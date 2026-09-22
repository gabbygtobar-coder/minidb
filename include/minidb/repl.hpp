#pragma once

#include "minidb/catalog.hpp"

#include <iosfwd>
#include <string>

namespace minidb {

// Options for the shell. An empty `path` keeps pages in memory and drops
// them when the shell exits. A non-empty path is a database file, created
// if it is missing or empty. The CLI always sets a path.
struct ReplOptions {
    std::string path;
};

// Line-oriented shell. Meta-commands are handled here. Every other non-empty
// line is parsed and executed against the open database.
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
    Database database_;
};

}  // namespace minidb
