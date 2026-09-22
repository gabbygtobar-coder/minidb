# MiniDB

MiniDB is a relational database engine written in C++. The engine is the project: storage, execution, and a small SQL surface built in this repository. It does not wrap, embed, or speak for PostgreSQL, SQLite, or any other existing database.

## Why

This is the systems piece of a CS portfolio, after WorthIt and ThreatLens. Those projects sit higher in the stack. MiniDB is the one that has to deal with files, memory, and query execution directly, in the open, one milestone at a time.

## Status

**M1 — SQL lexer and parser.** The tree configures with CMake, builds a `minidb` CLI, and runs GoogleTest on GitHub Actions. The shell keeps the M0 meta-commands (`.help`, `.exit`, `.quit`). A SQL line is tokenized and parsed into an AST, and the shell prints that AST or a parse error with a source location. Statements are not executed. There is no catalog and no storage.

## Stack

- C++17 (the code stays inside that dialect; a newer compiler is fine)
- CMake 3.16 or newer
- [GoogleTest](https://github.com/google/googletest) 1.15.2, downloaded by CMake FetchContent

GoogleTest is not a git submodule and it is not vendored. `FetchContent` pins the v1.15.2 release archive in `CMakeLists.txt` (URL plus SHA256). The version sits next to the test target, CI does not need a recursive clone, and googletest's history stays out of this repository. The first configure needs network access so CMake can download the archive. Later configures reuse CMake's fetch cache. Pass `-DBUILD_TESTING=OFF` to skip the download and build only the CLI.

## Roadmap

| Milestone | Planned scope | In this tree |
| --- | --- | --- |
| M0 | CMake, CLI stub, GoogleTest, CI | Yes |
| M1 | Lexer, parser, and AST for a tiny SQL subset | Yes |
| M2 | In-memory catalog and execution (`CREATE TABLE`, `INSERT`, `SELECT`) | No |
| M3 | Simple expressions and `WHERE` filters | No |
| M4 | On-disk pages and a buffer pool so data survives restart | No |
| M5 | One secondary index and a trivial access-path choice | No |

Milestones land in order. The parser does not execute statements and does not own a catalog.

## SQL subset

Keywords are case-insensitive. Identifiers keep the spelling from the source. The shell submits one line to the parser. A trailing semicolon is optional. The parser accepts newlines inside a statement when the whole statement is passed as one string; a statement split across shell lines is a parse error.

```text
statement    ::= create_table | drop_table | insert | select | update | delete [ ";" ]
create_table ::= "CREATE" "TABLE" identifier "(" column_def ( "," column_def )* ")"
column_def   ::= identifier type_name
type_name    ::= "INT" | "TEXT" | "BOOLEAN" | "FLOAT"
drop_table   ::= "DROP" "TABLE" identifier
insert       ::= "INSERT" "INTO" identifier "VALUES" "(" literal ( "," literal )* ")"
select       ::= "SELECT" ( "*" | identifier ( "," identifier )* ) "FROM" identifier [ where ]
update       ::= "UPDATE" identifier "SET" identifier "=" literal [ where ]
delete       ::= "DELETE" "FROM" identifier [ where ]
where        ::= "WHERE" identifier compare literal
compare      ::= "=" | "!=" | "<" | ">" | "<=" | ">="
literal      ::= integer | float | string | "TRUE" | "FALSE"
identifier   ::= [A-Za-z_][A-Za-z0-9_]*
integer      ::= [0-9]+
float        ::= [0-9]+ "." [0-9]* | "." [0-9]+
string       ::= "'" ( [^'] | "''" )* "'"
```

`TRUE` and `FALSE` are boolean literals. A doubled single quote inside a string is one quote character (`'it''s'` is `it's`). Keywords are reserved and cannot be used as names.

Not in this subset: `NULL`, comments, joins, aliases, expressions, `ORDER BY`, `GROUP BY`, column lists on `INSERT`, multi-row `INSERT`, multiple `SET` assignments, qualified names, and quoted identifiers.

## Build

Requirements: a C++17 compiler (g++ or clang++), CMake 3.16+, and network on the first configure when tests are enabled.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

The binary is `build/minidb`.

## Run

```bash
./build/minidb
./build/minidb /tmp/minidb-data
./build/minidb --help
```

The shell prints:

```text
MiniDB v0.1
Database: local
MiniDB>
```

With a path argument, the second line uses that path instead of `local`. The path is not created or read.

| Input | Result |
| --- | --- |
| `.help` | Lists meta-commands |
| `.exit`, `.quit` | Leaves the shell with status 0 |
| empty line | Another prompt; no error |
| one supported SQL statement | `Parsed: ` plus a one-line AST summary |
| other non-meta input | `Parse error at line:column: ...` |
| a `.` command other than the three above | `Unknown meta-command: ...` |

End of input (Ctrl-D) also exits with status 0. Meta-commands are case-sensitive. Surrounding whitespace is ignored.

```text
MiniDB> CREATE TABLE users (id INT, name TEXT);
Parsed: CreateTable users (id INT, name TEXT)
MiniDB> SELECT * FROM users WHERE id = 1;
Parsed: Select * FROM users WHERE id = 1
MiniDB> SELECT 1;
Parse error at 1:8: expected '*' or a column name, found integer '1'
```

The `Parsed:` line is the AST. It is not a query result.

## Test

```bash
cmake --build build
cd build && ctest --output-on-failure
```

`minidb_tests` covers the version string, the banner, meta-commands, the lexer (valid tokens, strings, numbers, and invalid input), the parser (each statement, type keywords, `WHERE` operators, and bad syntax), and the shell's AST / parse-error output.

## Layout

```text
include/minidb/     public headers (version, shell, tokens, AST, parser)
src/main.cpp        process entry
src/cli/            shell implementation
src/parser/         lexer and parser
tests/              GoogleTest
benchmarks/         placeholder until there is an engine to measure
docs/               architecture notes
examples/           sample shell session
```

## Limitations

- SQL is parsed only. A successful parse does not create a table, insert a row, or return results.
- No catalog, rows, indexes, planner, transactions, or concurrency control.
- No files are read or written. The data-directory argument is only copied into the banner.
- The shell submits one line to the parser. It does not accumulate a statement across lines.
- One process, one thread, stdin/stdout. There is no client/server protocol and no wire compatibility with PostgreSQL or MySQL.
- `benchmarks/` is a note, not a harness.
- Storage and execution have not started. A green test run means the skeleton, the lexer, and the parser behave as documented.

## License

[MIT](LICENSE).
