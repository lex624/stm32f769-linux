"""Send a camera-free LiveKit control sample and check the physical target reply."""
import argparse
import asyncio
import json
import os
import struct
import subprocess
import uuid

from livekit import api, rtc


async def smoke(args):
    room_name = "f769-" + uuid.uuid4().hex
    sender, receiver = rtc.Room(), rtc.Room()
    received = asyncio.get_running_loop().create_future()

    @receiver.on("data_received")
    def on_packet(packet):
        if (packet.topic == "controls" and packet.participant and
                packet.participant.identity == "controller" and not received.done()):
            received.set_result(bytes(packet.data))

    async def join(room, identity):
        token = api.AccessToken(os.environ["LIVEKIT_API_KEY"],
                                os.environ["LIVEKIT_API_SECRET"])
        token.with_identity(identity).with_grants(api.VideoGrants(room_join=True, room=room_name))
        await room.connect(args.url, token.to_jwt())

    try:
        await join(receiver, "board-gateway")
        await join(sender, "controller")
        async with asyncio.timeout(30):
            while "board-gateway" not in sender.remote_participants:
                await asyncio.sleep(0.1)
            payload = {"type": "gamepad", "data": {"left_y": 0.25, "right_x": -0.5}}
            # Membership arrives before SCTP is ready. Re-send this idempotent
            # diagnostic until the receiving participant confirms delivery.
            while not received.done():
                await sender.local_participant.publish_data(json.dumps(payload), reliable=True,
                    topic="controls", destination_identities=["board-gateway"])
                await asyncio.wait([received], timeout=1)
            message = json.loads(await received)
        if message != payload:
            raise ValueError("Unexpected LiveKit data")
        axes = message["data"]
        frame = struct.pack("<BBhh", 1, 42, round(1000 * axes["left_y"]), round(1000 * axes["right_x"]))
        reply = subprocess.run(["ssh", "-o", "BatchMode=yes", args.ssh, "/usr/bin/f769-control"],
                               input=frame, stdout=subprocess.PIPE, timeout=45, check=True).stdout
        expected = b"sequence=42 throttle=250 steering=-500"
        if expected not in reply.splitlines():
            raise RuntimeError(f"Missing board acknowledgement: {reply!r}")
        print("LiveKit controls -> Discovery:", expected.decode())
    finally:
        await sender.disconnect()
        await receiver.disconnect()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="ws://127.0.0.1:7880")
    parser.add_argument("--ssh", default="f769-disco")
    asyncio.run(smoke(parser.parse_args()))
