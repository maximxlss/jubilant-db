// Copyright 2024 Jubilant DB

#include "pager.h"

#include <stdexcept>
#include <utility>

namespace jubilant::storage {

Pager::Pager(std::filesystem::path data_path, std::uint32_t page_size)
    : data_path_(std::move(data_path)), page_size_(page_size) {}

Pager Pager::Open(const std::filesystem::path &data_path,
                  std::uint32_t page_size) {
  return Pager{data_path, page_size};
}

std::uint64_t Pager::Allocate(PageType type) {
  const std::uint64_t page_id = next_page_id_++;
  Page page{};
  page.id = page_id;
  page.type = type;
  page.payload.resize(page_size_);
  in_memory_pages_.emplace(page_id, std::move(page));
  return page_id;
}

void Pager::Write(const Page &page) {
  if (page.payload.size() != page_size_) {
    throw std::invalid_argument("Page payload size must equal page size.");
  }
  in_memory_pages_[page.id] = page;
}

std::optional<Page> Pager::Read(std::uint64_t page_id) const {
  auto it = in_memory_pages_.find(page_id);
  if (it == in_memory_pages_.end()) {
    return std::nullopt;
  }
  return it->second;
}

void Pager::Sync() const {
  // Persistent storage is not wired up yet; sync is a placeholder
  // to keep the build and API surface stable.
}

const std::filesystem::path &Pager::data_path() const noexcept {
  return data_path_;
}

std::uint32_t Pager::page_size() const noexcept { return page_size_; }

}  // namespace jubilant::storage
