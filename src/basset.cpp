#include <stdio.h>
#include <unistd.h>

#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include <linux/filter.h>
#include <linux/ptrace.h>
#include <linux/seccomp.h>

#undef PTRACE_CONT
#undef PTRACE_GETEVENTMSG
#undef PTRACE_GET_SYSCALL_INFO
#undef PTRACE_SETOPTIONS
#undef PTRACE_TRACEME

#include <cassert>
#include <csignal>
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

    if (wait(&wstatus) == -1) {
      perror("cannot wait()");
      return -1;
    }

    if (!WIFSTOPPED(wstatus) || WSTOPSIG(wstatus) != SIGSTOP) {
      cerr << "unexpected state of child\n";
      return -1;
    }

    if (ptrace(PTRACE_SETOPTIONS, pid, nullptr,
               PTRACE_O_TRACECLONE | PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK |
                   PTRACE_O_TRACEEXEC | PTRACE_O_TRACESECCOMP) == -1) {
      perror("cannot ptrace(PTRACE_SETOPTIONS)");
      return -1;
    }

    if (ptrace(PTRACE_CONT, pid, nullptr, nullptr) == -1) {
      perror("cannot ptrace(PTRACE_CONT)");
      return -1;
    }

    while (auto pid = wait(&wstatus)) {
      if (pid == -1) {
        perror("cannot wait()");
        return -1;
      }

      if (WIFEXITED(wstatus) || WIFSIGNALED(wstatus)) {
        cerr << pid << " exited/terminated by signal\n";
      } else if (WIFSTOPPED(wstatus)) {
        cerr << pid << " stopped\n";

        if (WSTOPSIG(wstatus) == SIGTRAP) {
          switch (wstatus >> 16) {
          case PTRACE_EVENT_CLONE:
          case PTRACE_EVENT_EXEC:
          case PTRACE_EVENT_FORK:
          case PTRACE_EVENT_VFORK: {
            unsigned long data;

            if (ptrace(PTRACE_GETEVENTMSG, pid, nullptr, &data) == -1) {
              perror("cannot ptrace(PTRACE_GETEVENTMSG)");
              return -1;
            }

            cerr << "event msg: " << data << '\n';
            break;
          }
          case PTRACE_EVENT_SECCOMP: {
            unsigned long ret_data;

            if (ptrace(PTRACE_GETEVENTMSG, pid, nullptr, &ret_data) == -1) {
              perror("cannot ptrace(PTRACE_GETEVENTMSG)");
              return -1;
            }

            cerr << "SECCOMP_RET_DATA: " << ret_data << '\n';

            ptrace_syscall_info data;
            auto res =
                ptrace(PTRACE_GET_SYSCALL_INFO, pid, sizeof(data), &data);

            if (res == -1) {
              perror("cannot ptrace(PTRACE_GET_SYSCALL_INFO)");
              return -1;
            }

            if (res > sizeof(data)) {
              cerr << "some data truncated\n";
            } else {
              assert(res > 0);

              switch (data.op) {
              case PTRACE_SYSCALL_INFO_SECCOMP: {
                cerr << "seccomp " << data.seccomp.nr << '\n';

                for (auto arg : data.seccomp.args) {
                  cerr << arg << ',';
                }

                cerr << '\n';
                assert(ret_data == data.seccomp.ret_data);
                break;
              }
              case PTRACE_SYSCALL_INFO_NONE:
              case PTRACE_SYSCALL_INFO_ENTRY:
              case PTRACE_SYSCALL_INFO_EXIT:
              default:
                cerr << "unexpected syscall operation: "
                     << static_cast<int>(data.op) << '\n';
                return -1;
              }
            }
            break;
          }
          default:
            cerr << "unknown stop event, signal: " << (wstatus >> 16) << '\n';
            return -1;
          }
        } else {
          cerr << "got signal: " << WSTOPSIG(wstatus) << '\n';
        }

        if (ptrace(PTRACE_CONT, pid, nullptr, nullptr) == -1) {
          perror("cannot ptrace(PTRACE_CONT)");
          return -1;
        }
      } else if (WIFCONTINUED(wstatus)) {
        cerr << pid << " continued\n";
      } else {
        cerr << "unexpected wait status: " << wstatus << '\n';
        return -1;
      }
    }

    return 0;
  }

  if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) == -1) {
    perror("cannot ptrace(PTRACE_TRACEME)");
    return -1;
  }

  sock_filter filter[] = {
      BPF_STMT(BPF_LD + BPF_W + BPF_ABS, offsetof(seccomp_data, nr)),
      BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, __NR_chdir, 0, 1),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_TRACE),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),
  };
  sock_fprog prog = {sizeof(filter) / sizeof(*filter), filter};

  if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
    perror("cannot prctl(PR_SET_NO_NEW_PRIVS)");
    return -1;
  }

  if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0) != 0) {
    perror("cannot prctl(PR_SET_SECCOMP)");
    return -1;
  }

  // allow parent to ptrace(PTRACE_SETOPTIONS)
  if (raise(SIGSTOP) != 0) {
    perror("cannot raise(SIGSTOP)");
    return -1;
  }

  execvp(*argv, argv);
  // on success, execve() does not return
  perror("cannot execve()");
  return -1;
}
