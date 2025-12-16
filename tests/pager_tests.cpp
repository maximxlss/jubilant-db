#include "storage/pager/pager.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

using jubilant::storage::kDefaultPageSize;
using jubilant::storage::Pager;
using jubilant::storage::PageType;

namespace {

fs::path TestPageFile() {
  return fs::temp_directory_path() / "jubilant-pager-tests.pages";
}

} // namespace

TEST(PagerTest, AllocatesPagesSequentially) {
  fs::remove(TestPageFile());
  auto pager = Pager::Open(TestPageFile(), kDefaultPageSize);
  const auto first = pager.Allocate(PageType::kLeaf);
  const auto second = pager.Allocate(PageType::kInternal);

  EXPECT_EQ(first, 0U);
  EXPECT_EQ(second, 1U);
}

TEST(PagerTest, WritesAndReadsPagePayload) {
  fs::remove(TestPageFile());
  auto pager = Pager::Open(TestPageFile(), kDefaultPageSize);
  const auto page_id = pager.Allocate(PageType::kLeaf);

  std::vector<std::byte> payload(pager.payload_size());
  payload[0] = std::byte{0xAB};

  jubilant::storage::Page page{};
  page.id = page_id;
  page.type = PageType::kLeaf;
  page.payload = payload;

  pager.Write(page);
  pager.Sync();

  const auto reopened = Pager::Open(TestPageFile(), kDefaultPageSize);
  const auto round_trip = reopened.Read(page_id);
  ASSERT_TRUE(round_trip.has_value());
  if (!round_trip.has_value()) {
    return;
  }
  const auto& round_trip_page = round_trip.value();
  EXPECT_EQ(round_trip_page.payload[0], std::byte{0xAB});
}

TEST(PagerTest, RejectsInvalidPageSize) {
  fs::remove(TestPageFile());
  auto pager = Pager::Open(TestPageFile(), kDefaultPageSize);
  jubilant::storage::Page bad{};
  bad.id = 0;
  bad.type = PageType::kLeaf;
  bad.payload.resize(10);

  EXPECT_THROW(pager.Write(bad), std::invalid_argument);
}
