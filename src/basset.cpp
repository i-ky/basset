#include <stdio.h>
#include <unistd.h>

#include <sys/ptrace.h>
#include <sys/wait.h>

#include <linux/ptrace.h>

#undef PTRACE_GET_SYSCALL_INFO
#undef PTRACE_SETOPTIONS
#undef PTRACE_SYSCALL
#undef PTRACE_TRACEME

#include <iostream>

using std::cerr;

int main(int argc, char *argv[]) {
  argv++;

  if (auto pid = fork()) {
    if (pid == -1) {
      perror("cannot fork()");
      return -1;
    }

    int wstatus;

    while (auto pid = wait(&wstatus)) {
      if (pid == -1) {
        perror("cannot wait()");
        return -1;
      }

      if (WIFEXITED(wstatus) || WIFSIGNALED(wstatus)) {
        cerr << pid << " exited/terminated by signal\n";
      } else if (WIFSTOPPED(wstatus)) {
        cerr << pid << " stopped\n";

        if (ptrace(PTRACE_SETOPTIONS, pid, nullptr,
                   PTRACE_O_TRACECLONE | PTRACE_O_TRACEFORK |
                       PTRACE_O_TRACEVFORK | PTRACE_O_TRACEEXEC |
                       PTRACE_O_TRACESYSGOOD) == -1) {
          perror("cannot ptrace(PTRACE_SETOPTIONS)");
        }

        ptrace_syscall_info data;

        if (auto res =
                ptrace(PTRACE_GET_SYSCALL_INFO, pid, sizeof(data), &data)) {
          if (res == -1) {
            perror("cannot ptrace(PTRACE_GET_SYSCALL_INFO)");
          } else if (res > sizeof(data)) {
            cerr << "some data truncated\n";
          } else {
            switch (data.op) {
            case PTRACE_SYSCALL_INFO_ENTRY:
              cerr << "entering syscall " << data.entry.nr << '\n';
              break;
            case PTRACE_SYSCALL_INFO_EXIT:
              cerr << "syscall returned " << data.exit.rval << '\n';
              break;
            case PTRACE_SYSCALL_INFO_SECCOMP:
            case PTRACE_SYSCALL_INFO_NONE:
            default:
              cerr << "unexpected syscall operation: "
                   << static_cast<int>(data.op) << '\n';
            }
          }
        }

        if (ptrace(PTRACE_SYSCALL, pid, nullptr, nullptr) == -1) {
          perror("cannot ptrace(PTRACE_SYSCALL)");
        }
      } else if (WIFCONTINUED(wstatus)) {
        cerr << pid << " continued\n";
      }
    }

    return 0;
  }

  if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) == -1) {
    perror("cannot ptrace(PTRACE_TRACEME)");
    return -1;
  }

  execvp(*argv, argv);
  // on success, execve() does not return
  perror("cannot execve()");
  return -1;
}
