# Examples

M2 executes SQL against an in-memory catalog. The rows below exist only until the process exits.

```text
$ minidb
MiniDB v0.1
Database: local
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

An optional argument is shown as the database name and is otherwise ignored:

```text
$ minidb /tmp/minidb-data
MiniDB v0.1
Database: /tmp/minidb-data
MiniDB> .quit
```

Blank lines are ignored. They do not print an error and they do not exit.
