#include <stdio.h>
#include <unistd.h>

#include <sys/ptrace.h>

int main(int argc, char *argv[], char *envp[]) {
  argv++;

  if (auto pid = fork()) {
    if (pid == -1) {
      perror("cannot fork()");
      return -1;
    }

    // TODO wait()

    return 0;
  }

  if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) == -1) {
    perror("cannot ptrace(PTRACE_TRACEME)");
    return -1;
  }

  execve(*argv, argv, envp);
  // on success, execve() does not return
  perror("cannot execve()");
  return -1;
}
