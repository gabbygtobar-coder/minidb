#pragma once

#include "minidb/catalog.hpp"

#include <iosfwd>
#include <string>

namespace minidb {

// Options for the shell. `database` is display-only. Nothing is opened on
// disk; tables created in the session live in `Repl`'s memory and are
// discarded when the process exits.
struct ReplOptions {
    std::string database{"local"};
};

// Line-oriented shell. Meta-commands are handled here. Every other non-empty
// line is parsed and executed against an in-memory database.
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
