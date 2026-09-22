# Examples

M0 only has the shell stub. This session is what `minidb` does today.

```text
$ minidb
MiniDB v0.1
Database: local
MiniDB> .help
MiniDB meta-commands:
  .help          Show this message
  .exit          Exit the shell
  .quit          Exit the shell

No SQL is available in M0. Other input is rejected until M1.
MiniDB> SELECT 1;
SQL engine is not implemented yet (M1+).
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
