#include "storage/pager/pager.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>
#include <span>

#include "storage/checksum.h"

namespace jubilant::storage {

Pager::Pager(std::filesystem::path data_path, std::uint32_t page_size, int fd,
             std::uint64_t next_page)
    : data_path_(std::move(data_path)),
      page_size_(page_size),
      payload_size_(page_size - sizeof(PageHeader)),
      next_page_id_(next_page),
      fd_(fd) {}

Pager::Pager(Pager &&other) noexcept
    : data_path_(std::move(other.data_path_)),
      page_size_(other.page_size_),
      payload_size_(other.payload_size_),
      next_page_id_(other.next_page_id_),
      fd_(other.fd_) {
  other.fd_ = -1;
}

Pager &Pager::operator=(Pager &&other) noexcept {
  if (this == &other) {
    return *this;
  }

  if (fd_ >= 0) {
    ::close(fd_);
  }

  data_path_ = std::move(other.data_path_);
  page_size_ = other.page_size_;
  payload_size_ = other.payload_size_;
  next_page_id_ = other.next_page_id_;
  fd_ = other.fd_;
  other.fd_ = -1;
  return *this;
}

Pager::~Pager() {
  if (fd_ >= 0) {
    ::close(fd_);
  }
}

Pager Pager::Open(const std::filesystem::path &data_path,
                  std::uint32_t page_size) {
  if (page_size <= sizeof(PageHeader)) {
    throw std::invalid_argument("page_size too small for header");
  }

  if (!data_path.parent_path().empty()) {
    std::filesystem::create_directories(data_path.parent_path());
  }

  const int fd =
      ::open(data_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0644);
  if (fd < 0) {
    throw std::runtime_error("Failed to open page file");
  }

  const off_t file_size = ::lseek(fd, 0, SEEK_END);
  if (file_size < 0) {
    ::close(fd);
    throw std::runtime_error("Failed to seek page file");
  }
  if (file_size % page_size != 0) {
    ::close(fd);
    throw std::runtime_error("Page file is corrupt (size mismatch)");
  }

  const std::uint64_t next_page =
      file_size == 0 ? 0 : static_cast<std::uint64_t>(file_size) / page_size;

  return Pager{data_path, page_size, fd, next_page};
}

std::uint64_t Pager::Allocate(PageType type) {
  const std::uint64_t page_id = next_page_id_++;
  Page page{};
  page.id = page_id;
  page.type = type;
  page.payload.assign(payload_size_, std::byte{0});
  Write(page);
  return page_id;
}

void Pager::Write(const Page &page) {
  if (page.payload.size() != payload_size_) {
    throw std::invalid_argument("Page payload size must equal payload size.");
  }

  std::vector<std::byte> buffer(page_size_);
  PageHeader header{};
  header.id = page.id;
  header.type = static_cast<std::uint16_t>(page.type);
  header.crc = ComputeCrc(page.payload);

  std::memcpy(buffer.data(), &header, sizeof(PageHeader));
  std::memcpy(buffer.data() + sizeof(PageHeader), page.payload.data(),
              payload_size_);

  const auto written =
      ::pwrite(fd_, buffer.data(), buffer.size(), OffsetFor(page.id));
  if (written != static_cast<ssize_t>(buffer.size())) {
    throw std::runtime_error("Failed to write page to disk");
  }
}

std::optional<Page> Pager::Read(std::uint64_t page_id) const {
  if (page_id >= next_page_id_) {
    return std::nullopt;
  }

  std::vector<std::byte> buffer(page_size_);
  const auto read = ::pread(fd_, buffer.data(), buffer.size(),
                            OffsetFor(page_id));
  if (read != static_cast<ssize_t>(buffer.size())) {
    return std::nullopt;
  }

  return ParsePage(buffer, payload_size_);
}

void Pager::Sync() const { ::fsync(fd_); }

std::uint64_t Pager::page_count() const noexcept { return next_page_id_; }

std::uint32_t Pager::payload_size() const noexcept { return payload_size_; }

const std::filesystem::path &Pager::data_path() const noexcept {
  return data_path_;
}

std::uint32_t Pager::page_size() const noexcept { return page_size_; }

std::uint64_t Pager::OffsetFor(std::uint64_t page_id) const {
  return page_id * static_cast<std::uint64_t>(page_size_);
}

std::uint32_t Pager::ComputeCrc(const std::vector<std::byte> &payload) {
  return ComputeCrc32(std::span<const std::byte>(payload.data(),
                                                 payload.size()));
}

Page Pager::ParsePage(const std::vector<std::byte> &buffer,
                      std::uint32_t payload_size) {
  if (buffer.size() < sizeof(PageHeader) + payload_size) {
    throw std::runtime_error("Corrupt page buffer");
  }

  PageHeader header{};
  std::memcpy(&header, buffer.data(), sizeof(PageHeader));
  std::vector<std::byte> payload(payload_size);
  std::memcpy(payload.data(), buffer.data() + sizeof(PageHeader),
              payload_size);

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
