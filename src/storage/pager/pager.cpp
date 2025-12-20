#include "storage/pager/pager.h"

#include "storage/checksum.h"

#include <cstring>
#include <fcntl.h>
#include <iterator>
#include <span>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace jubilant::storage {

Pager::Pager(PagerConfig config)
    : data_path_(std::move(config.data_path)), page_size_(config.page_size),
      payload_size_(PayloadSizeFor(config.page_size)), next_page_id_(config.next_page),
      file_descriptor_(config.file_descriptor) {}

Pager::Pager(Pager&& other) noexcept
    : data_path_(std::move(other.data_path_)), page_size_(other.page_size_),
      payload_size_(other.payload_size_), next_page_id_(other.next_page_id_),
      file_descriptor_(other.file_descriptor_) {
  other.file_descriptor_ = -1;
}

Pager& Pager::operator=(Pager&& other) noexcept {
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
  CloseFileDescriptor();
}

Pager Pager::Open(const std::filesystem::path& data_path, std::uint32_t page_size) {
  if (page_size <= HeaderSize()) {
    throw std::invalid_argument("page_size too small for header");
  }

  if (!data_path.parent_path().empty()) {
    std::filesystem::create_directories(data_path.parent_path());
  }

  const int opened_fd = ::open(data_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0644);
  if (opened_fd < 0) {
    throw std::runtime_error("Failed to open page file");
  }

  const off_t file_size = ::lseek(opened_fd, 0, SEEK_END);
  if (file_size < 0) {
    ::close(opened_fd);
    throw std::runtime_error("Failed to seek page file");
  }
  if (file_size % page_size != 0) {
    ::close(opened_fd);
    throw std::runtime_error("Page file is corrupt (size mismatch)");
  }

  const PageId next_page = file_size == 0 ? 0 : static_cast<PageId>(file_size) / page_size;

  return Pager{PagerConfig{
      .data_path = data_path,
      .page_size = page_size,
      .file_descriptor = opened_fd,
      .next_page = next_page,
  }};
}

PageId Pager::Allocate(PageType type) {
  const PageId page_id = next_page_id_++;
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
  header.lsn = page.lsn;
  header.crc = ComputeCrc(header, page.payload);

  std::memcpy(buffer.data(), &header, HeaderSize());
  std::memcpy(buffer.data() + HeaderSize(), page.payload.data(), payload_size_);

  const auto offset = static_cast<off_t>(OffsetFor(page.id));
  const auto expected_size = std::ssize(buffer);
  const auto written = ::pwrite(file_descriptor_, buffer.data(), buffer.size(), offset);
  if (written != expected_size) {
    throw std::runtime_error("Failed to write page to disk");
  }
}

std::optional<Page> Pager::Read(PageId page_id) const {
  if (page_id >= next_page_id_) {
    return std::nullopt;
  }

  std::vector<std::byte> buffer(page_size_);
  const auto offset = static_cast<off_t>(OffsetFor(page_id));
  const auto expected_size = std::ssize(buffer);
  const auto read = ::pread(file_descriptor_, buffer.data(), buffer.size(), offset);
  if (read != expected_size) {
    return std::nullopt;
  }

  return ParsePage(buffer, payload_size_);
}

void Pager::Sync() const {
  ::fsync(file_descriptor_);
}

PageId Pager::page_count() const noexcept {
  return next_page_id_;
}

std::uint32_t Pager::payload_size() const noexcept {
  return payload_size_;
}

const std::filesystem::path& Pager::data_path() const noexcept {
  return data_path_;
}

std::uint32_t Pager::page_size() const noexcept {
  return page_size_;
}

std::uint64_t Pager::OffsetFor(PageId page_id) const {
  return page_id * static_cast<std::uint64_t>(page_size_);
}

std::uint32_t Pager::ComputeCrc(const PageHeader& header, const std::vector<std::byte>& payload) {
  PageHeader header_copy = header;
  header_copy.crc = 0;

  std::vector<std::byte> crc_buffer(HeaderSize() + payload.size());
  std::memcpy(crc_buffer.data(), &header_copy, HeaderSize());
  std::memcpy(crc_buffer.data() + HeaderSize(), payload.data(), payload.size());
  return ComputeCrc32(std::span<const std::byte>(crc_buffer.data(), crc_buffer.size()));
}

Page Pager::ParsePage(const std::vector<std::byte>& buffer, std::uint32_t payload_size) {
  if (buffer.size() < HeaderSize() + payload_size) {
    throw std::runtime_error("Corrupt page buffer");
  }

  PageHeader header{};
  std::memcpy(&header, buffer.data(), HeaderSize());
  std::vector<std::byte> payload(payload_size);
  std::memcpy(payload.data(), buffer.data() + HeaderSize(), payload_size);

  if (ComputeCrc(header, payload) != header.crc) {
    throw std::runtime_error("Page checksum mismatch");
  }

  Page page{};
  page.id = header.id;
  page.type = static_cast<PageType>(header.type);
  page.lsn = header.lsn;
  page.payload = std::move(payload);
  return page;
}

void Pager::CloseFileDescriptor() {
  if (file_descriptor_ >= 0) {
    ::close(file_descriptor_);
    file_descriptor_ = -1;
  }
}

} // namespace jubilant::storage
