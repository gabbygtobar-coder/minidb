# Examples

The shell runs SQL against a database file. The same path still has the tables and indexes after the process exits. `.explain` prints the scan and does not run the statement. Build first (`cmake -S . -B build && cmake --build build`). The transcript below matches `./build/minidb`.

```text
$ rm -f /tmp/minidb-demo.db
$ ./build/minidb /tmp/minidb-demo.db
MiniDB v0.1
Database: /tmp/minidb-demo.db
MiniDB> CREATE TABLE users (id INT, name TEXT, active BOOLEAN, score FLOAT);
Created table users.
MiniDB> INSERT INTO users VALUES (1, 'ada', TRUE, 3.14);
Inserted 1 row.
MiniDB> INSERT INTO users VALUES (2, 'grace', FALSE, 10.0);
Inserted 1 row.
MiniDB> .explain SELECT id, name FROM users WHERE id = 1
Seq Scan on users
  Filter: id = 1
MiniDB> CREATE INDEX idx_id ON users (id);
Created index idx_id on users(id).
MiniDB> .indexes
idx_id ON users (id)
MiniDB> .explain SELECT id, name FROM users WHERE id = 1
Index Scan using idx_id on users
  Index Cond: id = 1
MiniDB> SELECT id, name FROM users WHERE id = 1;
id | name
---+-----
1  | ada
(1 row)
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
MiniDB> .indexes
idx_id ON users (id)
MiniDB> .explain SELECT id, name FROM users WHERE id = 1
Index Scan using idx_id on users
  Index Cond: id = 1
MiniDB> SELECT id, name FROM users WHERE id = 1;
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

With no argument, the file is `minidb.db` in the current directory. Each successful statement is flushed with `fflush`. That is not an `fsync`. A crash can still lose or tear the last write. See [docs/storage.md](../docs/storage.md) and [docs/index.md](../docs/index.md).

Blank lines are ignored. They do not print an error and they do not exit.
