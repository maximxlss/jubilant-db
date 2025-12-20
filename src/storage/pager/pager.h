#pragma once

#include "storage/storage_common.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace jubilant::storage {

struct Page {
  PageId id{};
  PageType type{PageType::kUnknown};
  Lsn lsn{0};
  std::vector<std::byte> payload;
};

class Pager {
  struct PageHeader;

public:
  static Pager Open(const std::filesystem::path& data_path,
                    std::uint32_t page_size = kDefaultPageSize);

  [[nodiscard]] static constexpr std::uint32_t HeaderSize() noexcept {
    return sizeof(PageHeader);
  }
  [[nodiscard]] static constexpr std::uint32_t PayloadSizeFor(std::uint32_t page_size) noexcept {
    return page_size > HeaderSize() ? page_size - HeaderSize() : 0;
  }

  PageId Allocate(PageType type);
  void Write(const Page& page);
  [[nodiscard]] std::optional<Page> Read(PageId page_id) const;
  void Sync() const;

  Pager(const Pager&) = delete;
  Pager& operator=(const Pager&) = delete;
  Pager(Pager&& other) noexcept;
  Pager& operator=(Pager&& other) noexcept;
  ~Pager();

  [[nodiscard]] PageId page_count() const noexcept;
  [[nodiscard]] std::uint32_t payload_size() const noexcept;

  [[nodiscard]] const std::filesystem::path& data_path() const noexcept;
  [[nodiscard]] std::uint32_t page_size() const noexcept;

private:
  struct PagerConfig {
    std::filesystem::path data_path;
    std::uint32_t page_size;
    int file_descriptor;
    PageId next_page;
  };

  explicit Pager(PagerConfig config);

  struct PageHeader {
    PageId id{0};
    Lsn lsn{0};
    std::uint16_t type{0};
    std::uint16_t reserved{0};
    std::uint32_t crc{0};
  };

  std::filesystem::path data_path_;
  std::uint32_t page_size_;
  std::uint32_t payload_size_;
  PageId next_page_id_{0};
  int file_descriptor_{-1};

  [[nodiscard]] std::uint64_t OffsetFor(PageId page_id) const;
  [[nodiscard]] static std::uint32_t ComputeCrc(const PageHeader& header,
                                                const std::vector<std::byte>& payload);
  [[nodiscard]] static Page ParsePage(const std::vector<std::byte>& buffer,
                                      std::uint32_t payload_size);
  void CloseFileDescriptor();
};

} // namespace jubilant::storage
