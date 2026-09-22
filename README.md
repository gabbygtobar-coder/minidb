# MiniDB

MiniDB is a relational database engine written in C++. The engine is the project: storage, execution, and a small SQL surface built in this repository. It does not wrap, embed, or speak for PostgreSQL, SQLite, or any other existing database.

## Why

This is the systems piece of a CS portfolio, after WorthIt and ThreatLens. Those projects sit higher in the stack. MiniDB is the one that has to deal with files, memory, and query execution directly, in the open, one milestone at a time.

## Status

**M2 — in-memory catalog and execution.** The shell parses one SQL statement and runs it against tables that live in the process. `CREATE TABLE`, `DROP TABLE`, `INSERT`, `SELECT`, `UPDATE`, and `DELETE` work, including `WHERE`. Data is discarded when the process exits. Persistence is M3; this milestone does not write page files.

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
| M2 | In-memory catalog and execution, including `WHERE` | Yes |
| M3 | On-disk pages so data survives restart | No |
| M4 | One secondary index and a trivial access-path choice | No |

Milestones land in order. The parser still does not execute statements. The executor consumes the AST from the outside.

## SQL subset

Keywords are case-insensitive. Identifiers keep the spelling from the source and are matched case-sensitively (`Users` and `users` are different tables). The shell submits one line to the parser. A trailing semicolon is optional. The parser accepts newlines inside a statement when the whole statement is passed as one string; a statement split across shell lines is a parse error.

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

## Execution

The executor type-checks every literal against the column it writes or compares. There is no coercion.

| Column | Accepted literal |
| --- | --- |
| `INT` | integer (`1`, `007`) |
| `FLOAT` | float (`1.0`, `.5`, `10.`) |
| `TEXT` | string |
| `BOOLEAN` | `TRUE` or `FALSE` |

An integer literal is not a `FLOAT`. A failed `INSERT` or `UPDATE` does not change the table.

`WHERE` uses the same type rule. `INT` and `FLOAT` allow `=`, `!=`, `<`, `>`, `<=`, and `>=`. `FLOAT` compares the stored IEEE value with no tolerance. `TEXT` and `BOOLEAN` allow only `=` and `!=`. `TEXT` equality is byte-wise and case-sensitive (`'Ada'` is not `'ada'`). Ordering a `TEXT` or `BOOLEAN` column is an error.

Rows stay in insertion order. `SELECT` prints an aligned table and a row count. `.tables` lists names in lexicographic order. `.schema <table>` prints a one-line `CREATE TABLE` for that table.

A mistake (missing table, duplicate table or column, unknown column, wrong `INSERT` arity, wrong type, illegal comparison) prints `Error: ...` and returns to the prompt. A syntax error still prints `Parse error at line:column: ...`. Neither exits the process.

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

With a path argument, the second line uses that path instead of `local`. The path is not created or read. Tables from the session are gone when the process exits.

| Input | Result |
| --- | --- |
| `.help` | Lists meta-commands |
| `.tables` | Table names, or `(no tables)` |
| `.schema <table>` | `CREATE TABLE ...` for that table |
| `.exit`, `.quit` | Leaves the shell with status 0 |
| empty line | Another prompt; no error |
| one supported SQL statement | Runs it and prints a message or a result table |
| other non-meta input | `Parse error at line:column: ...` |
| a `.` command other than the ones above | `Unknown meta-command: ...` |

End of input (Ctrl-D) also exits with status 0. Meta-commands are case-sensitive. Surrounding whitespace is ignored.

```text
MiniDB> CREATE TABLE users (id INT, name TEXT, active BOOLEAN, score FLOAT);
Created table users.
MiniDB> INSERT INTO users VALUES (1, 'ada', TRUE, 3.14);
Inserted 1 row.
MiniDB> INSERT INTO users VALUES (2, 'grace', FALSE, 10.0);
Inserted 1 row.
MiniDB> SELECT * FROM users;
id | name  | active | score
---+-------+--------+------
1  | ada   | TRUE   | 3.14
2  | grace | FALSE  | 10.0
(2 rows)
MiniDB> UPDATE users SET name = 'ada lovelace' WHERE id = 1;
Updated 1 row.
MiniDB> DELETE FROM users WHERE active = FALSE;
Deleted 1 row.
MiniDB> SELECT id, name FROM users;
id | name
---+-------------
1  | ada lovelace
(1 row)
MiniDB> .tables
users
MiniDB> .schema users
CREATE TABLE users (id INT, name TEXT, active BOOLEAN, score FLOAT);
MiniDB> DROP TABLE users;
Dropped table users.
MiniDB> SELECT 1;
Parse error at 1:8: expected '*' or a column name, found integer '1'
```

## Test

```bash
cmake --build build
cd build && ctest --output-on-failure
```

`minidb_tests` covers the version string, the shell, the lexer, the parser, and execution: create, insert, select, update, delete, `WHERE` on each type, type and arity errors, drop, and schema listing.

## Layout

```text
include/minidb/     public headers (version, shell, tokens, AST, catalog, executor)
src/main.cpp        process entry
src/cli/            shell implementation
src/parser/         lexer and parser
src/catalog/        in-memory tables and cell values
src/executor/       statement execution
tests/              GoogleTest
benchmarks/         placeholder until there is a storage path to measure
docs/               architecture notes
examples/           sample shell session
```

## Limitations

- Tables live in memory for one process. Exit, and the data is gone. M3 is the milestone that writes it to disk.
- The optional data-directory argument is only copied into the banner. No file is created or read.
- No indexes, planner, transactions, or concurrency control.
- No `NULL`, joins, expressions, `ORDER BY`, or multi-row `INSERT`.
- The shell submits one line to the parser. It does not accumulate a statement across lines.
- One process, one thread, stdin/stdout. There is no client/server protocol and no wire compatibility with PostgreSQL or MySQL.
- `benchmarks/` is a note, not a harness.

## License

[MIT](LICENSE).
