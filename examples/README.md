# Examples

M3 executes SQL against a database file. The same path still has the tables after the process exits.

```text
$ ./build/minidb /tmp/minidb-demo.db
MiniDB v0.1
Database: /tmp/minidb-demo.db
MiniDB> CREATE TABLE users (id INT, name TEXT, active BOOLEAN, score FLOAT);
Created table users.
MiniDB> INSERT INTO users VALUES (1, 'ada', TRUE, 3.14);
Inserted 1 row.
MiniDB> INSERT INTO users VALUES (2, 'grace', FALSE, 10.0);
Inserted 1 row.
MiniDB> SELECT * FROM users WHERE id >= 1;
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
MiniDB> .exit
```

Start the shell again on that file:

```text
$ ./build/minidb /tmp/minidb-demo.db
MiniDB v0.1
Database: /tmp/minidb-demo.db
MiniDB> .tables
users
MiniDB> SELECT id, name FROM users;
id | name
---+-------------
1  | ada lovelace
(1 row)
MiniDB> DROP TABLE users;
Dropped table users.
MiniDB> .exit
```

A bad statement prints an error and leaves the shell running:

```text
MiniDB> INSERT INTO users VALUES (1, 'ada');
Error: No such table: users
MiniDB> SELECT 1;
Parse error at 1:8: expected '*' or a column name, found integer '1'
```

With no argument, the file is `minidb.db` in the current directory. Each successful statement is flushed with `fflush`. That is not an `fsync`. A crash can still lose or tear the last write. See [docs/storage.md](../docs/storage.md).

Blank lines are ignored. They do not print an error and they do not exit.
