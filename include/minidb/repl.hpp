#pragma once

#include <iosfwd>
#include <string>

namespace minidb {

// Options for the M0 shell. `database` is display-only; nothing is opened.
struct ReplOptions {
    std::string database{"local"};
};

// Line-oriented shell. Meta-commands are handled here. SQL is not parsed.
class Repl {
  public:
    Repl(std::istream& in, std::ostream& out, ReplOptions options = {});

    // Runs until `.exit`, `.quit`, or end of input. Returns a process status.
    int run();

  private:
    void print_banner();
    void print_help();

    std::istream& in_;
    std::ostream& out_;
    ReplOptions options_;
};

}  // namespace minidb
