# Mini DBMS in C

A small database management system implemented in **C** for a university Database Programming project.

## Features
* CRUD operations for multiple related tables
* CSV-based storage
* Primary-key hash indexing with linear probing
* Primary key, foreign key, unique, and check constraints
* Referential integrity validation
* Shared/read and exclusive/write locking
* Concurrent access using `pthread` mutexes and condition variables
* Audit logging
* Command-line interface
* Dynamic memory management with `malloc` and `realloc`
