# Examples

M1 parses SQL and prints an AST. It does not execute it.

```text
$ minidb
MiniDB v0.1
Database: local
MiniDB> .help
MiniDB meta-commands:
  .help          Show this message
  .exit          Exit the shell
  .quit          Exit the shell

SQL statements are parsed and printed as an AST. They are not executed.
MiniDB> CREATE TABLE users (id INT, name TEXT);
Parsed: CreateTable users (id INT, name TEXT)
MiniDB> INSERT INTO users VALUES (1, 'ada');
Parsed: Insert users VALUES (1, 'ada')
MiniDB> SELECT * FROM users WHERE id = 1;
Parsed: Select * FROM users WHERE id = 1
MiniDB> SELECT 1;
Parse error at 1:8: expected '*' or a column name, found integer '1'
MiniDB> .exit
```

An optional argument is shown as the database name and is otherwise ignored:

```text
$ minidb /tmp/minidb-data
MiniDB v0.1
Database: /tmp/minidb-data
MiniDB> .quit
```

Blank lines are ignored. They do not print an error and they do not exit.
