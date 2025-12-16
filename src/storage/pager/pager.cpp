#include "storage/pager/pager.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <compare>
#include <cstring>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "storage/checksum.h"

namespace jubilant::storage {

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Pager::Pager(std::filesystem::path data_path, std::uint32_t page_size, int file_descriptor,
             std::uint64_t next_page)
    : data_path_(std::move(data_path)),
      page_size_(page_size),
      payload_size_(page_size - sizeof(PageHeader)),
      next_page_id_(next_page),
      file_descriptor_(file_descriptor) {}

Pager::Pager(Pager&& other) noexcept
    : data_path_(std::move(other.data_path_)),
      page_size_(other.page_size_),
      payload_size_(other.payload_size_),
      next_page_id_(other.next_page_id_),
      file_descriptor_(other.file_descriptor_) {
  other.file_descriptor_ = -1;
}

auto Pager::operator=(Pager&& other) noexcept -> Pager& {
  if (this == &other) {
    return *this;
  }

  if (file_descriptor_ >= 0) {
    ::close(file_descriptor_);
  }

  data_path_ = std::move(other.data_path_);
  page_size_ = other.page_size_;
  payload_size_ = other.payload_size_;
  next_page_id_ = other.next_page_id_;
  file_descriptor_ = other.file_descriptor_;
  other.file_descriptor_ = -1;
  return *this;
}

Pager::~Pager() {
  if (file_descriptor_ >= 0) {
    ::close(file_descriptor_);
  }
}

auto Pager::Open(const std::filesystem::path& data_path, std::uint32_t page_size) -> Pager {
  if (page_size <= sizeof(PageHeader)) {
    throw std::invalid_argument("page_size too small for header");
  }

  if (!data_path.parent_path().empty()) {
    std::filesystem::create_directories(data_path.parent_path());
  }

  const int file_descriptor = ::open(data_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0644);
  if (file_descriptor < 0) {
    throw std::runtime_error("Failed to open page file");
  }

  const off_t file_size = ::lseek(file_descriptor, 0, SEEK_END);
  if (file_size < 0) {
    ::close(file_descriptor);
    throw std::runtime_error("Failed to seek page file");
  }
  if (file_size % page_size != 0) {
    ::close(file_descriptor);
    throw std::runtime_error("Page file is corrupt (size mismatch)");
  }

  const std::uint64_t next_page =
      file_size == 0 ? 0 : static_cast<std::uint64_t>(file_size) / page_size;

  return Pager{data_path, page_size, file_descriptor, next_page};
}

auto Pager::Allocate(PageType type) -> std::uint64_t {
  const std::uint64_t page_id = next_page_id_++;
  Page page{};
  page.id = page_id;
  page.type = type;
  page.payload.assign(payload_size_, std::byte{0});
  Write(page);
  return page_id;
}

void Pager::Write(const Page& page) {
  if (page.payload.size() != payload_size_) {
    throw std::invalid_argument("Page payload size must equal payload size.");
  }

  std::vector<std::byte> buffer(page_size_);
  PageHeader header{};
  header.id = page.id;
  header.type = static_cast<std::uint16_t>(page.type);
  header.crc = ComputeCrc(page.payload);

  std::memcpy(buffer.data(), &header, sizeof(PageHeader));
  std::memcpy(buffer.data() + sizeof(PageHeader), page.payload.data(), payload_size_);

  const auto written = ::pwrite(file_descriptor_, buffer.data(), buffer.size(),
                                static_cast<off_t>(OffsetFor(page.id)));
  if (std::cmp_not_equal(written, static_cast<ssize_t>(buffer.size()))) {
    throw std::runtime_error("Failed to write page to disk");
  }
}

auto Pager::Read(std::uint64_t page_id) const -> std::optional<Page> {
  if (page_id >= next_page_id_) {
    return std::nullopt;
  }

  std::vector<std::byte> buffer(page_size_);
  const auto read = ::pread(file_descriptor_, buffer.data(), buffer.size(),
                            static_cast<off_t>(OffsetFor(page_id)));
  if (std::cmp_not_equal(read, static_cast<ssize_t>(buffer.size()))) {
    return std::nullopt;
  }

  return ParsePage(buffer, payload_size_);
}

void Pager::Sync() const { ::fsync(file_descriptor_); }

auto Pager::page_count() const noexcept -> std::uint64_t { return next_page_id_; }

auto Pager::payload_size() const noexcept -> std::uint32_t { return payload_size_; }

auto Pager::data_path() const noexcept -> const std::filesystem::path& { return data_path_; }

auto Pager::page_size() const noexcept -> std::uint32_t { return page_size_; }

auto Pager::OffsetFor(std::uint64_t page_id) const -> std::uint64_t {
  return page_id * static_cast<std::uint64_t>(page_size_);
}

auto Pager::ComputeCrc(const std::vector<std::byte>& payload) -> std::uint32_t {
  return ComputeCrc32(std::span<const std::byte>(payload.data(), payload.size()));
}

auto Pager::ParsePage(const std::vector<std::byte>& buffer, std::uint32_t payload_size) -> Page {
  if (buffer.size() < sizeof(PageHeader) + payload_size) {
    throw std::runtime_error("Corrupt page buffer");
  }

  PageHeader header{};
  std::memcpy(&header, buffer.data(), sizeof(PageHeader));
  std::vector<std::byte> payload(payload_size);
  std::memcpy(payload.data(), buffer.data() + sizeof(PageHeader), payload_size);

  if (ComputeCrc(payload) != header.crc) {
    throw std::runtime_error("Page checksum mismatch");
  }

  Page page{};
  page.id = header.id;
  page.type = static_cast<PageType>(header.type);
  page.payload = std::move(payload);
  return page;
}

}  // namespace jubilant::storage
