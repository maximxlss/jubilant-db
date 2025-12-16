// Copyright 2024 Jubilant DB
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <vector>

namespace jubilant::storage {

constexpr std::uint32_t kDefaultPageSize = 4096;

enum class PageType : std::uint16_t {
  kUnknown = 0,
  kLeaf = 1,
  kInternal = 2,
  kManifest = 3,
};

struct Page {
  std::uint64_t id{};
  PageType type{PageType::kUnknown};
  std::vector<std::byte> payload;
};

class Pager {
 public:
  static Pager Open(const std::filesystem::path &data_path,
                    std::uint32_t page_size = kDefaultPageSize);

  std::uint64_t Allocate(PageType type);
  void Write(const Page &page);
  [[nodiscard]] std::optional<Page> Read(std::uint64_t page_id) const;
  void Sync() const;

  [[nodiscard]] const std::filesystem::path &data_path() const noexcept;
  [[nodiscard]] std::uint32_t page_size() const noexcept;

 private:
  Pager(std::filesystem::path data_path, std::uint32_t page_size);

  std::filesystem::path data_path_;
  std::uint32_t page_size_;
  std::uint64_t next_page_id_{0};
  std::unordered_map<std::uint64_t, Page> in_memory_pages_;
};

}  // namespace jubilant::storage
