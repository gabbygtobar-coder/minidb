#include "minidb/version.hpp"

#include <gtest/gtest.h>

#include <string>

TEST(Version, ReportsRelease) {
    EXPECT_FALSE(minidb::kVersion.empty());
    EXPECT_EQ(std::string(minidb::kVersion), "0.1");
}
