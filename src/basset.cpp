#include <fcntl.h>
#include <regex.h>
#include <unistd.h>

#include <sys/ptrace.h>
#include <sys/wait.h>

#include <linux/limits.h>

#include <csignal>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

using std::cerr;
using std::cout;
using std::function;
using std::ifstream;
using std::make_unique;
using std::ofstream;
using std::ostream;
using std::string;
using std::to_string;
using std::unordered_map;
using std::unordered_set;
using std::vector;
using std::filesystem::canonical;
using std::filesystem::path;
using std::literals::string_literals::operator""s;

class Pipe {
public:
  Pipe();
  Pipe(const Pipe &) = delete;
  Pipe(Pipe &&) = delete;
  Pipe &operator=(const Pipe &) = delete;
  Pipe &operator=(Pipe &&) = delete;
  ~Pipe();
  template <typename T> Pipe &operator>>(T &dst);
  template <typename T> Pipe &operator<<(const T &src);

private:
  void read(char *dst, size_t size);
  void write(const char *src, size_t size);
  int fds[2];
};

Pipe::Pipe() {
  if (pipe2(fds, O_CLOEXEC) == -1) {
    perror("cannot pipe2()");
    throw;
  }
}

Pipe::~Pipe() {
  for (auto fd : fds) {
    if (close(fd) == -1) {
      perror("cannot close()");
    }
  }
}

template <typename T> Pipe &Pipe::operator>>(T &dst) {
  read(&dst, sizeof(dst));
  return *this;
}

template <typename T> Pipe &Pipe::operator<<(const T &src) {
  write(&src, sizeof(src));
  return *this;
}

void Pipe::read(char *dst, size_t size) {
  while (size > 0) {
    auto ret = ::read(fds[0], dst, size);

    switch (ret) {
    case -1:
      perror("cannot read()");
      throw;
    case 0:
      throw;
    default:
      dst += ret;
      size -= ret;
    }
  }
}

void Pipe::write(const char *src, size_t size) {
  while (size > 0) {
    auto ret = ::write(fds[1], src, size);

    if (ret == -1) {
      perror("cannot write()");
      throw;
    }

    src += ret;
    size -= ret;
  }
}

class Regex {
public:
  explicit Regex(const char *pattern);
  Regex(const Regex &) = delete;
  Regex(Regex &&) = delete;
  Regex &operator=(const Regex &) = delete;
  Regex &operator=(Regex &&) = delete;
  ~Regex();
  [[nodiscard]] bool match(const string &text) const;

private:
  [[noreturn]] void report(int errcode) const;
  regex_t preg;
};

Regex::Regex(const char *pattern) {
  auto errcode = regcomp(&preg, pattern, REG_EXTENDED | REG_NOSUB);

  if (errcode != 0) {
    report(errcode);
  }
}

Regex::~Regex() { regfree(&preg); }

bool Regex::match(const string &text) const {
  auto errcode = regexec(&preg, text.c_str(), 0, nullptr, 0);

  switch (errcode) {
  case 0:
    return true;
  case REG_NOMATCH:
    return false;
  default:
    report(errcode);
  }
}

void Regex::report(int errcode) const {
  auto size = regerror(errcode, &preg, nullptr, 0);
  auto errbuf = make_unique<char>(size);
  regerror(errcode, &preg, errbuf.get(), size);
  cerr << errbuf.get() << '\n';
  throw;
}

class CompilationDatabase {
public:
  using IsSourceFileFunc = function<bool(const string &)>;

  CompilationDatabase(string filename, IsSourceFileFunc is_source_file);

  CompilationDatabase(const CompilationDatabase &) = delete;
  CompilationDatabase(CompilationDatabase &&) = delete;
  CompilationDatabase &operator=(const CompilationDatabase &) = delete;
  CompilationDatabase &operator=(CompilationDatabase &&) = delete;
  ~CompilationDatabase() = default;

  bool load();
  bool save();

  void add(const string &directory, const vector<string> &command);

private:
  const string filename;
  const IsSourceFileFunc is_source_file;

  static path make_index_key(const string &directory,
                               const string &filename) {
    return canonical(path(directory) / filename);
  }

  // compilatiopn database entry
  struct Entry {
    string directory;
    string file;
    vector<string> arguments;
  };

  // maps canonized source file path to entry
  unordered_map<path, Entry> index;
};

CompilationDatabase::CompilationDatabase(string filename,
                                         IsSourceFileFunc is_source_file)
    : filename(std::move(filename)), is_source_file(std::move(is_source_file)) {
}

bool CompilationDatabase::load() {
  ifstream ifs(filename);
  if (!ifs) {
    return true;
  }

  try {
    auto json = nlohmann::json::parse(ifs);

    // load existing database with removing entries for no longer existing
    // sources
    // (https://github.com/rizsotto/Bear/wiki/Features#append-to-existing-jspn-cdb)
    for (auto&& entry : json) {
      const auto directory = entry["directory"].get<string>();
      const auto file = entry["file"].get<string>();

      if (exists(path(directory) / file)) {
        index[make_index_key(directory, file)] =
            {directory, file, entry["arguments"]};
      }
    }
  } catch (const nlohmann::json::parse_error &e) {
    cerr << "json error: " << e.what() << '\n';
    return false;
  }

  return true;
}

bool CompilationDatabase::save() {
  ofstream ofs(filename);
  if (!ofs) {
    return false;
  }

  auto json = nlohmann::json::array();
  for (const auto &kv : index) {
    json += nlohmann::json::object({{"directory", kv.second.directory},
                                    {"file", kv.second.file},
                                    {"arguments", kv.second.arguments}});
  }

  ofs << json.dump(4) << '\n';
  return true;
}

void CompilationDatabase::add(const string &directory,
                              const vector<string> &command) {
  for (auto &&argument : command) {
    if (!is_source_file(argument)) {
      continue;
    }

    // append new entry or update already existing one
    index.insert_or_assign(
        make_index_key(directory, argument),
        Entry{directory, argument, command});
  }
}

bool is_source_file(const string &argument) {
  // file extensions associated with C, C++, Objective-C, Objective-C++
  // https://github.com/github/linguist/blob/master/lib/linguist/languages.yml
  static const unordered_set<string> extensions{
      "c"s,    "cats"s, "h"s,   "idc"s, "cpp"s, "c++"s, "cc"s,  "cp"s,
      "cppm"s, "cxx"s,  "h++"s, "hh"s,  "hpp"s, "hxx"s, "inc"s, "inl"s,
      "ino"s,  "ipp"s,  "ixx"s, "re"s,  "tcc"s, "tpp"s, "m"s,   "mm"s};

  auto dot = argument.find_last_of('.');

  if (dot == string::npos) {
    return false;
  }

  auto extension = argument.substr(dot + 1);

  return extensions.count(extension) != 0;
}

int main(int argc, char *argv[]) {
  using token = char;

  const string progname{*argv++};

  auto usage = [progname](ostream &stream) {
    stream << progname << " [options] -- ...\n";
  };

  bool verbose{false};
  string output{"compile_commands.json"};

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
    } else if (*argv == "--output"s) {
      if (*++argv == nullptr) {
        cerr << "--output requires a value\n";
        return -1;
      }

      output = *argv;
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

  Pipe pipe;

  if (auto main_pid = fork()) {
    if (main_pid == -1) {
      perror("cannot fork()");
      return -1;
    }

    if (ptrace(PTRACE_SEIZE, main_pid, nullptr,
               PTRACE_O_TRACECLONE | PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK |
                   PTRACE_O_TRACEEXEC | PTRACE_O_EXITKILL) == -1) {
      perror("cannot ptrace(PTRACE_SEIZE)");
      return -1;
    }

    // signal to the child that everything is set up
    pipe << token{};

    Regex compiler(
        R"REGEX(([^-]+-)*(c(c|\+\+)|(g(cc|\+\+)|clang(\+\+)?)(-[0-9]+(\.[0-9]+){0,2})?)$)REGEX");

    CompilationDatabase cdb(output, is_source_file);
    if (!cdb.load()) {
      cerr << "cannot load '" << output << "'\n";
      return -1;
    }

    int wstatus;

    while (auto pid = wait(&wstatus)) {
      if (pid == -1) {
        perror("cannot wait()");
        return -1;
      }

      if (WIFEXITED(wstatus) || WIFSIGNALED(wstatus)) {
        verbose &&cerr << pid << " exited/terminated by signal\n";

        if (pid == main_pid) {
          if (!cdb.save()) {
            cerr << "cannot save '" << output << "'\n";
            return -1;
          }

          if (WIFEXITED(wstatus)) {
            return WEXITSTATUS(wstatus);
          }

          if (raise(WTERMSIG(wstatus)) != 0) {
            perror("cannot raise()");
            return -1;
          }

          // for some reason the signal terminated the child, but did not kill
          // the parent...
          return -1;
        }
      } else if (WIFSTOPPED(wstatus)) {
        verbose &&cerr << pid << " stopped\n";

        auto signal = WSTOPSIG(wstatus);

        if (signal == SIGTRAP) {
          switch (wstatus >> 16) {
          case PTRACE_EVENT_EXEC: {
            char exe[PATH_MAX];
            auto ret = readlink(("/proc/" + to_string(pid) + "/exe").c_str(),
                                exe, sizeof(exe));

            if (ret == -1) {
              perror("cannot readlink(\"/proc/[pid]/exe\")");
              return -1;
            }

            string executable(exe, ret);

            if (!compiler.match(executable)) {
              break;
            }

            char cwd[PATH_MAX];
            ret = readlink(("/proc/" + to_string(pid) + "/cwd").c_str(), cwd,
                           sizeof(cwd));

            if (ret == -1) {
              perror("cannot readlink(\"/proc/[pid]/cwd\")");
              return -1;
            }

            ifstream cmdline("/proc/" + to_string(pid) + "/cmdline");
            vector<string> cmd;

            for (string arg; getline(cmdline, arg, '\0');) {
              cmd.emplace_back(arg);
            }

            if (!cmdline.eof()) {
              cerr << "failed to read /proc/[pid]/cmdline\n";
              return -1;
            }

            if (ptrace(PTRACE_DETACH, pid, nullptr, nullptr) == -1) {
              perror("cannot ptrace(PTRACE_DETACH)");
              return -1;
            }

            cdb.add(string(cwd, ret), cmd);

            continue;
          }
          case PTRACE_EVENT_CLONE:
          case PTRACE_EVENT_FORK:
          case PTRACE_EVENT_VFORK:
          case PTRACE_EVENT_STOP:
            break;
          default:
            cerr << "unknown stop event, signal: " << (wstatus >> 16) << '\n';
            return -1;
          }

          signal = 0;
        } else {
          verbose &&cerr << "got signal: " << signal << '\n';
        }

        if (ptrace(PTRACE_CONT, pid, nullptr, signal) == -1) {
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

  token t;

  // block until parent signals readiness
  pipe >> t;

  execvp(*argv, argv);
  // on success, execve() does not return
  perror("cannot execve()");
  return -1;
}
