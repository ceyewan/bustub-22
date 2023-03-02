//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager_instance.cpp
//
// Identification: src/buffer/buffer_pool_manager.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/buffer_pool_manager_instance.h"

#include "common/exception.h"
#include "common/macros.h"

namespace bustub {

BufferPoolManagerInstance::BufferPoolManagerInstance(size_t pool_size,
                                                     DiskManager *disk_manager,
                                                     size_t replacer_k,
                                                     LogManager *log_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager),
      log_manager_(log_manager) {
  // we allocate a consecutive memory space for the buffer pool
  pages_ = new Page[pool_size_];
  page_table_ = new ExtendibleHashTable<page_id_t, frame_id_t>(bucket_size_);
  replacer_ = new LRUKReplacer(pool_size, replacer_k);

  // Initially, every page is in the free list.
  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_.emplace_back(static_cast<int>(i));
  }

  // TODO(students): remove this line after you have implemented the buffer pool
  // manager
  //  throw NotImplementedException(
  //      "BufferPoolManager is not implemented yet. If you have finished
  //      implementing BPM, please remove the throw " "exception line in
  //      `buffer_pool_manager_instance.cpp`.");
}

BufferPoolManagerInstance::~BufferPoolManagerInstance() {
  delete[] pages_;
  delete page_table_;
  delete replacer_;
}

auto BufferPoolManagerInstance::NewPgImp(page_id_t *page_id) -> Page * {
  latch_.lock();
  frame_id_t frame_id = -1;
  if (!free_list_.empty()) {
    frame_id = free_list_.front();
    free_list_.pop_front();
  } else if (replacer_->Evict(&frame_id)) {
    if (pages_[frame_id].IsDirty()) {
      disk_manager_->WritePage(pages_[frame_id].GetPageId(),
                               pages_[frame_id].GetData());
    }
    page_table_->Remove(pages_[frame_id].GetPageId());
  } else {
    page_id = nullptr;
    latch_.unlock();
    return nullptr;
  }
  *page_id = AllocatePage();
  page_table_->Insert(*page_id, frame_id);
  pages_[frame_id].page_id_ = *page_id;
  pages_[frame_id].pin_count_ = 1;
  pages_[frame_id].is_dirty_ = false;
  pages_[frame_id].ResetMemory();
  replacer_->RecordAccess(frame_id);
  replacer_->SetEvictable(frame_id, false);
  auto p = &pages_[frame_id];
  latch_.unlock();
  return p;
}

auto BufferPoolManagerInstance::FetchPgImp(page_id_t page_id) -> Page * {
  latch_.lock();
  frame_id_t frame_id = -1;
  if (page_table_->Find(page_id, frame_id)) {
    replacer_->RecordAccess(frame_id);
    replacer_->SetEvictable(frame_id, false);
    pages_[frame_id].pin_count_ += 1;
    auto p = &pages_[frame_id];
    latch_.unlock();
    return p;
  }
  if (!free_list_.empty()) {
    frame_id = free_list_.front();
    free_list_.pop_front();
  } else if (replacer_->Evict(&frame_id)) {
    if (pages_[frame_id].IsDirty()) {
      disk_manager_->WritePage(pages_[frame_id].GetPageId(),
                               pages_[frame_id].GetData());
    }
    page_table_->Remove(pages_[frame_id].GetPageId());
  } else {
    latch_.unlock();
    return nullptr;
  }
  page_table_->Insert(page_id, frame_id);
  pages_[frame_id].page_id_ = page_id;
  pages_[frame_id].pin_count_ = 1;
  pages_[frame_id].is_dirty_ = false;
  pages_[frame_id].ResetMemory();
  disk_manager_->ReadPage(page_id, pages_[frame_id].GetData());
  replacer_->RecordAccess(frame_id);
  replacer_->SetEvictable(frame_id, false);
  auto p = &pages_[frame_id];
  latch_.unlock();
  return p;
}

auto BufferPoolManagerInstance::UnpinPgImp(page_id_t page_id, bool is_dirty)
    -> bool {
  latch_.lock();
  frame_id_t frame_id;
  if (!page_table_->Find(page_id, frame_id) ||
      pages_[frame_id].GetPinCount() == 0) {
    latch_.unlock();
    return false;
  }
  pages_[frame_id].pin_count_--;
  if (is_dirty) {
    pages_[frame_id].is_dirty_ = is_dirty;
  }
  if (pages_[frame_id].GetPinCount() == 0) {
    replacer_->SetEvictable(frame_id, true);
  }
  latch_.unlock();
  return true;
}

auto BufferPoolManagerInstance::FlushPgImp(page_id_t page_id) -> bool {
  latch_.lock();
  frame_id_t frame_id;
  if (!page_table_->Find(page_id, frame_id)) {
    latch_.unlock();
    return false;
  }
  disk_manager_->WritePage(pages_[frame_id].GetPageId(),
                           pages_[frame_id].GetData());
  pages_->is_dirty_ = false;
  latch_.unlock();
  return true;
}
void BufferPoolManagerInstance::FlushAllPgsImp() {
  latch_.lock();
  frame_id_t tmp;
  for (size_t frame_id = 0; frame_id < pool_size_; frame_id++) {
    if (page_table_->Find(pages_[frame_id].GetPageId(), tmp)) {
      disk_manager_->WritePage(pages_[frame_id].GetPageId(),
                               pages_[frame_id].GetData());
      pages_->is_dirty_ = false;
    }
  }
  latch_.unlock();
}

auto BufferPoolManagerInstance::DeletePgImp(page_id_t page_id) -> bool {
  latch_.lock();
  DeallocatePage(page_id);
  frame_id_t frame_id;
  if (!page_table_->Find(page_id, frame_id)) {
    latch_.unlock();
    return true;
  }
  if (pages_[frame_id].GetPinCount() > 0) {
    latch_.unlock();
    return false;
  }
  if (pages_[frame_id].IsDirty()) {
    disk_manager_->WritePage(pages_[frame_id].GetPageId(),
                             pages_[frame_id].GetData());
    pages_->is_dirty_ = false;
  }
  replacer_->Remove(frame_id);
  free_list_.push_back(frame_id);
  page_table_->Remove(page_id);
  latch_.unlock();
  return true;
}

auto BufferPoolManagerInstance::AllocatePage() -> page_id_t {
  return next_page_id_++;
}

} // namespace bustub
