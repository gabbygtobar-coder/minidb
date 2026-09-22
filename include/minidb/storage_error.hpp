#pragma once

#include <stdexcept>
#include <string>

namespace minidb {

// A broken database file, a short read, or an I/O failure in the pager.
// User mistakes (bad SQL, a row that cannot fit on a page) are ExecutionError.
class StorageError : public std::runtime_error {
  public:
    explicit StorageError(const std::string& message) : std::runtime_error(message) {}
};

}  // namespace minidb
