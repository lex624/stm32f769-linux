/*
 * f769-hello - bring-up test for the STM32F769I-DISCO running uClinux.
 *
 * Deliberately exercises the noMMU-specific bits: there is no fork() on this
 * target, so a child is spawned with vfork()+execvp(). After vfork() the parent
 * is suspended and the child shares the parent's address space, so the child
 * must touch nothing before exec.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/wait.h>

static void print_uname(void)
{
	struct utsname u;

	if (uname(&u) != 0) {
		perror("uname");
		return;
	}
	printf("kernel   : %s %s %s\n", u.sysname, u.release, u.machine);
	printf("hostname : %s\n", u.nodename);
}

static void print_model(void)
{
	char buf[128];
	FILE *f = fopen("/proc/device-tree/model", "r");

	if (!f)
		return;
	if (fgets(buf, sizeof(buf), f))
		printf("board    : %s\n", buf);
	fclose(f);
}

static void print_meminfo(void)
{
	char line[128];
	FILE *f = fopen("/proc/meminfo", "r");

	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		if (!strncmp(line, "MemTotal:", 9) ||
		    !strncmp(line, "MemFree:", 8)) {
			line[strcspn(line, "\n")] = '\0';
			printf("%s\n", line);
		}
	}
	fclose(f);
}

/* fork() is unavailable without an MMU; vfork()+exec is the supported path. */
static int spawn_child(void)
{
	char *argv[] = { "/bin/true", NULL };
	pid_t pid;
	int status;

	pid = vfork();
	if (pid < 0) {
		perror("vfork");
		return -1;
	}
	if (pid == 0) {
		execvp(argv[0], argv);
		_exit(127);	/* _exit, not exit: we share the parent's state */
	}
	if (waitpid(pid, &status, 0) < 0) {
		perror("waitpid");
		return -1;
	}

	printf("vfork+exec: child %d exited %d\n", (int)pid,
	       WIFEXITED(status) ? WEXITSTATUS(status) : -1);
	return 0;
}

int main(void)
{
	puts("=== f769-hello: STM32F769I-DISCO bring-up ===");
	print_uname();
	print_model();
	print_meminfo();
	printf("page size: %ld bytes\n", sysconf(_SC_PAGESIZE));

	return spawn_child() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
