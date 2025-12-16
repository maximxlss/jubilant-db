#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
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
  std::uint64_t lsn{0};
  std::vector<std::byte> payload;
};

class Pager {
public:
  static Pager Open(const std::filesystem::path& data_path,
                    std::uint32_t page_size = kDefaultPageSize);

  std::uint64_t Allocate(PageType type);
  void Write(const Page& page);
  [[nodiscard]] std::optional<Page> Read(std::uint64_t page_id) const;
  void Sync() const;

  Pager(const Pager&) = delete;
  Pager& operator=(const Pager&) = delete;
  Pager(Pager&& other) noexcept;
  Pager& operator=(Pager&& other) noexcept;
  ~Pager();

  [[nodiscard]] std::uint64_t page_count() const noexcept;
  [[nodiscard]] std::uint32_t payload_size() const noexcept;

  [[nodiscard]] const std::filesystem::path& data_path() const noexcept;
  [[nodiscard]] std::uint32_t page_size() const noexcept;

private:
  struct PagerConfig {
    std::filesystem::path data_path;
    std::uint32_t page_size;
    int file_descriptor;
    std::uint64_t next_page;
  };

  explicit Pager(PagerConfig config);

  struct PageHeader {
    std::uint64_t id{0};
    std::uint64_t lsn{0};
    std::uint16_t type{0};
    std::uint16_t reserved{0};
    std::uint32_t crc{0};
  };

  std::filesystem::path data_path_;
  std::uint32_t page_size_;
  std::uint32_t payload_size_;
  std::uint64_t next_page_id_{0};
  int file_descriptor_{-1};

  [[nodiscard]] std::uint64_t OffsetFor(std::uint64_t page_id) const;
  [[nodiscard]] static std::uint32_t ComputeCrc(const PageHeader& header,
                                                const std::vector<std::byte>& payload);
  [[nodiscard]] static Page ParsePage(const std::vector<std::byte>& buffer,
                                      std::uint32_t payload_size);
  void CloseFileDescriptor();
};

} // namespace jubilant::storage
