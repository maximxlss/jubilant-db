#include <filesystem>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

#include "storage/pager/pager.h"

namespace fs = std::filesystem;

using jubilant::storage::PageType;
using jubilant::storage::Pager;
using jubilant::storage::kDefaultPageSize;

namespace {

fs::path TestPageFile() {
  return fs::temp_directory_path() / "jubilant-pager-tests.pages";
}

}  // namespace

TEST(PagerTest, AllocatesPagesSequentially) {
  auto pager = Pager::Open(TestPageFile(), kDefaultPageSize);
  const auto first = pager.Allocate(PageType::kLeaf);
  const auto second = pager.Allocate(PageType::kInternal);

  EXPECT_EQ(first, 0u);
  EXPECT_EQ(second, 1u);
}

TEST(PagerTest, WritesAndReadsPagePayload) {
  auto pager = Pager::Open(TestPageFile(), kDefaultPageSize);
  const auto page_id = pager.Allocate(PageType::kLeaf);

  std::vector<std::byte> payload(kDefaultPageSize);
  payload[0] = std::byte{0xAB};

  jubilant::storage::Page page{};
  page.id = page_id;
  page.type = PageType::kLeaf;
  page.payload = payload;

  pager.Write(page);
  const auto round_trip = pager.Read(page_id);
  ASSERT_TRUE(round_trip.has_value());
  EXPECT_EQ(round_trip->payload[0], std::byte{0xAB});
}

TEST(PagerTest, RejectsInvalidPageSize) {
  auto pager = Pager::Open(TestPageFile(), kDefaultPageSize);
  jubilant::storage::Page bad{};
  bad.id = 0;
  bad.type = PageType::kLeaf;
  bad.payload.resize(10);

  EXPECT_THROW(pager.Write(bad), std::invalid_argument);
}
