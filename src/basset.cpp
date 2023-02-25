#include <stdio.h>
#include <unistd.h>

#include <sys/ptrace.h>
#include <sys/wait.h>

#include <linux/limits.h>
#include <linux/ptrace.h>

#undef PTRACE_CONT
#undef PTRACE_SETOPTIONS
#undef PTRACE_TRACEME

#include <csignal>
#include <fstream>
#include <iostream>
#include <ostream>
#include <string>

using std::cerr;
using std::cout;
using std::ifstream;
using std::ostream;
using std::string;
using std::to_string;
using std::literals::string_literals::operator""s;

int main(int argc, char *argv[]) {
  const string progname{*argv++};

  auto usage = [progname](ostream &stream) {
    stream << progname << " [options] -- ...\n";
  };

  bool verbose{false};

  while (*argv != nullptr) {
    if (*argv == "--"s) {
      break;
    }

    if (*argv == "--help"s) {
      usage(cout);
      return 0;
    }

    if (*argv == "--verbose"s) {
      verbose = true;
    } else if (*argv == "--no-verbose"s) {
      verbose = false;
    } else {
      cerr << "unsupported option: " << *argv << '\n';
      usage(cerr);
      return -1;
    }

    argv++;
  }

  if (*argv == nullptr) {
    cerr << "unexpected end of arguments\n";
    usage(cerr);
    return -1;
  }

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
                   PTRACE_O_TRACEEXEC | PTRACE_O_EXITKILL) == -1) {
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
        verbose &&cerr << pid << " exited/terminated by signal\n";
      } else if (WIFSTOPPED(wstatus)) {
        verbose &&cerr << pid << " stopped\n";

        if (WSTOPSIG(wstatus) == SIGTRAP) {
          switch (wstatus >> 16) {
          case PTRACE_EVENT_EXEC: {
            char exe[PATH_MAX];
            auto ret = readlink(("/proc/" + to_string(pid) + "/exe").c_str(),
                                exe, sizeof(exe));

            if (ret == -1) {
              perror("cannot readlink(\"/proc/[pid]/exe\")");
              return -1;
            }

            cerr << string(exe, ret) << '\n';

            char cwd[PATH_MAX];
            ret = readlink(("/proc/" + to_string(pid) + "/cwd").c_str(), cwd,
                           sizeof(cwd));

            if (ret == -1) {
              perror("cannot readlink(\"/proc/[pid]/cwd\")");
              return -1;
            }

            cerr << string(cwd, ret) << '\n';

            ifstream cmdline("/proc/" + to_string(pid) + "/cmdline");

            for (string arg; getline(cmdline, arg, '\0');) {
              cerr << '\t' << arg.data() << '\n';
            }

            if (!cmdline.eof()) {
              cerr << "failed to read /proc/[pid]/cmdline\n";
              return -1;
            }

            break;
          }
          case PTRACE_EVENT_CLONE:
          case PTRACE_EVENT_FORK:
          case PTRACE_EVENT_VFORK:
            break;
          default:
            cerr << "unknown stop event, signal: " << (wstatus >> 16) << '\n';
            return -1;
          }
        } else {
          verbose &&cerr << "got signal: " << WSTOPSIG(wstatus) << '\n';
        }

        if (ptrace(PTRACE_CONT, pid, nullptr, nullptr) == -1) {
          perror("cannot ptrace(PTRACE_CONT)");
          return -1;
        }
      } else if (WIFCONTINUED(wstatus)) {
        verbose &&cerr << pid << " continued\n";
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
