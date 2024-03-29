// Copyright 2013 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <map>
#include <vector>

#include "global-handles.h"

#include "cctest.h"

using namespace v8::internal;
using v8::UniqueId;


static List<Object*> skippable_objects;
static List<Object*> can_skip_called_objects;


static bool CanSkipCallback(Heap* heap, Object** pointer) {
  can_skip_called_objects.Add(*pointer);
  return skippable_objects.Contains(*pointer);
}


static void ResetCanSkipData() {
  skippable_objects.Clear();
  can_skip_called_objects.Clear();
}


class TestRetainedObjectInfo : public v8::RetainedObjectInfo {
 public:
  TestRetainedObjectInfo() : has_been_disposed_(false) {}

  bool has_been_disposed() { return has_been_disposed_; }

  virtual void Dispose() {
    ASSERT(!has_been_disposed_);
    has_been_disposed_ = true;
  }

  virtual bool IsEquivalent(v8::RetainedObjectInfo* other) {
    return other == this;
  }

  virtual intptr_t GetHash() { return 0; }

  virtual const char* GetLabel() { return "whatever"; }

 private:
  bool has_been_disposed_;
};


class TestObjectVisitor : public ObjectVisitor {
 public:
  virtual void VisitPointers(Object** start, Object** end) {
    for (Object** o = start; o != end; ++o)
      visited.Add(*o);
  }

  List<Object*> visited;
};


TEST(IterateObjectGroupsOldApi) {
  CcTest::InitializeVM();
  GlobalHandles* global_handles = Isolate::Current()->global_handles();

  v8::HandleScope handle_scope(CcTest::isolate());

  Handle<Object> g1s1 =
      global_handles->Create(HEAP->AllocateFixedArray(1)->ToObjectChecked());
  Handle<Object> g1s2 =
      global_handles->Create(HEAP->AllocateFixedArray(1)->ToObjectChecked());

  Handle<Object> g2s1 =
      global_handles->Create(HEAP->AllocateFixedArray(1)->ToObjectChecked());
  Handle<Object> g2s2 =
      global_handles->Create(HEAP->AllocateFixedArray(1)->ToObjectChecked());

  TestRetainedObjectInfo info1;
  TestRetainedObjectInfo info2;
  {
    Object** g1_objects[] = { g1s1.location(), g1s2.location() };
    Object** g2_objects[] = { g2s1.location(), g2s2.location() };

    global_handles->AddObjectGroup(g1_objects, 2, &info1);
    global_handles->AddObjectGroup(g2_objects, 2, &info2);
  }

  // Iterate the object groups. First skip all.
  {
    ResetCanSkipData();
    skippable_objects.Add(*g1s1.location());
    skippable_objects.Add(*g1s2.location());
    skippable_objects.Add(*g2s1.location());
    skippable_objects.Add(*g2s2.location());
    TestObjectVisitor visitor;
    global_handles->IterateObjectGroups(&visitor, &CanSkipCallback);

    // CanSkipCallback was called for all objects.
    ASSERT(can_skip_called_objects.length() == 4);
    ASSERT(can_skip_called_objects.Contains(*g1s1.location()));
    ASSERT(can_skip_called_objects.Contains(*g1s2.location()));
    ASSERT(can_skip_called_objects.Contains(*g2s1.location()));
    ASSERT(can_skip_called_objects.Contains(*g2s2.location()));

    // Nothing was visited.
    ASSERT(visitor.visited.length() == 0);
    ASSERT(!info1.has_been_disposed());
    ASSERT(!info2.has_been_disposed());
  }

  // Iterate again, now only skip the second object group.
  {
    ResetCanSkipData();
    // The first grough should still be visited, since only one object is
    // skipped.
    skippable_objects.Add(*g1s1.location());
    skippable_objects.Add(*g2s1.location());
    skippable_objects.Add(*g2s2.location());
    TestObjectVisitor visitor;
    global_handles->IterateObjectGroups(&visitor, &CanSkipCallback);

    // CanSkipCallback was called for all objects.
    ASSERT(can_skip_called_objects.length() == 3 ||
           can_skip_called_objects.length() == 4);
    ASSERT(can_skip_called_objects.Contains(*g1s2.location()));
    ASSERT(can_skip_called_objects.Contains(*g2s1.location()));
    ASSERT(can_skip_called_objects.Contains(*g2s2.location()));

    // The first group was visited.
    ASSERT(visitor.visited.length() == 2);
    ASSERT(visitor.visited.Contains(*g1s1.location()));
    ASSERT(visitor.visited.Contains(*g1s2.location()));
    ASSERT(info1.has_been_disposed());
    ASSERT(!info2.has_been_disposed());
  }

  // Iterate again, don't skip anything.
  {
    ResetCanSkipData();
    TestObjectVisitor visitor;
    global_handles->IterateObjectGroups(&visitor, &CanSkipCallback);

    // CanSkipCallback was called for all objects.
    ASSERT(can_skip_called_objects.length() == 1);
    ASSERT(can_skip_called_objects.Contains(*g2s1.location()) ||
           can_skip_called_objects.Contains(*g2s2.location()));

    // The second group was visited.
    ASSERT(visitor.visited.length() == 2);
    ASSERT(visitor.visited.Contains(*g2s1.location()));
    ASSERT(visitor.visited.Contains(*g2s2.location()));
    ASSERT(info2.has_been_disposed());
  }
}


TEST(IterateObjectGroups) {
  CcTest::InitializeVM();
  GlobalHandles* global_handles = Isolate::Current()->global_handles();

  v8::HandleScope handle_scope(CcTest::isolate());

  Handle<Object> g1s1 =
      global_handles->Create(HEAP->AllocateFixedArray(1)->ToObjectChecked());
  Handle<Object> g1s2 =
      global_handles->Create(HEAP->AllocateFixedArray(1)->ToObjectChecked());

  Handle<Object> g2s1 =
      global_handles->Create(HEAP->AllocateFixedArray(1)->ToObjectChecked());
  Handle<Object> g2s2 =
    global_handles->Create(HEAP->AllocateFixedArray(1)->ToObjectChecked());

  TestRetainedObjectInfo info1;
  TestRetainedObjectInfo info2;
  global_handles->SetObjectGroupId(g2s1.location(), UniqueId(2));
  global_handles->SetObjectGroupId(g2s2.location(), UniqueId(2));
  global_handles->SetRetainedObjectInfo(UniqueId(2), &info2);
  global_handles->SetObjectGroupId(g1s1.location(), UniqueId(1));
  global_handles->SetObjectGroupId(g1s2.location(), UniqueId(1));
  global_handles->SetRetainedObjectInfo(UniqueId(1), &info1);

  // Iterate the object groups. First skip all.
  {
    ResetCanSkipData();
    skippable_objects.Add(*g1s1.location());
    skippable_objects.Add(*g1s2.location());
    skippable_objects.Add(*g2s1.location());
    skippable_objects.Add(*g2s2.location());
    TestObjectVisitor visitor;
    global_handles->IterateObjectGroups(&visitor, &CanSkipCallback);

    // CanSkipCallback was called for all objects.
    ASSERT(can_skip_called_objects.length() == 4);
    ASSERT(can_skip_called_objects.Contains(*g1s1.location()));
    ASSERT(can_skip_called_objects.Contains(*g1s2.location()));
    ASSERT(can_skip_called_objects.Contains(*g2s1.location()));
    ASSERT(can_skip_called_objects.Contains(*g2s2.location()));

    // Nothing was visited.
    ASSERT(visitor.visited.length() == 0);
    ASSERT(!info1.has_been_disposed());
    ASSERT(!info2.has_been_disposed());
  }

  // Iterate again, now only skip the second object group.
  {
    ResetCanSkipData();
    // The first grough should still be visited, since only one object is
    // skipped.
    skippable_objects.Add(*g1s1.location());
    skippable_objects.Add(*g2s1.location());
    skippable_objects.Add(*g2s2.location());
    TestObjectVisitor visitor;
    global_handles->IterateObjectGroups(&visitor, &CanSkipCallback);

    // CanSkipCallback was called for all objects.
    ASSERT(can_skip_called_objects.length() == 3 ||
           can_skip_called_objects.length() == 4);
    ASSERT(can_skip_called_objects.Contains(*g1s2.location()));
    ASSERT(can_skip_called_objects.Contains(*g2s1.location()));
    ASSERT(can_skip_called_objects.Contains(*g2s2.location()));

    // The first group was visited.
    ASSERT(visitor.visited.length() == 2);
    ASSERT(visitor.visited.Contains(*g1s1.location()));
    ASSERT(visitor.visited.Contains(*g1s2.location()));
    ASSERT(info1.has_been_disposed());
    ASSERT(!info2.has_been_disposed());
  }

  // Iterate again, don't skip anything.
  {
    ResetCanSkipData();
    TestObjectVisitor visitor;
    global_handles->IterateObjectGroups(&visitor, &CanSkipCallback);

    // CanSkipCallback was called for all objects.
    ASSERT(can_skip_called_objects.length() == 1);
    ASSERT(can_skip_called_objects.Contains(*g2s1.location()) ||
           can_skip_called_objects.Contains(*g2s2.location()));

    // The second group was visited.
    ASSERT(visitor.visited.length() == 2);
    ASSERT(visitor.visited.Contains(*g2s1.location()));
    ASSERT(visitor.visited.Contains(*g2s2.location()));
    ASSERT(info2.has_been_disposed());
  }
}


TEST(ImplicitReferences) {
  CcTest::InitializeVM();
  GlobalHandles* global_handles = Isolate::Current()->global_handles();

  v8::HandleScope handle_scope(CcTest::isolate());

  Handle<Object> g1s1 =
      global_handles->Create(HEAP->AllocateFixedArray(1)->ToObjectChecked());
  Handle<Object> g1c1 =
      global_handles->Create(HEAP->AllocateFixedArray(1)->ToObjectChecked());
  Handle<Object> g1c2 =
      global_handles->Create(HEAP->AllocateFixedArray(1)->ToObjectChecked());


  Handle<Object> g2s1 =
      global_handles->Create(HEAP->AllocateFixedArray(1)->ToObjectChecked());
  Handle<Object> g2s2 =
    global_handles->Create(HEAP->AllocateFixedArray(1)->ToObjectChecked());
  Handle<Object> g2c1 =
    global_handles->Create(HEAP->AllocateFixedArray(1)->ToObjectChecked());

  global_handles->SetObjectGroupId(g1s1.location(), UniqueId(1));
  global_handles->SetObjectGroupId(g2s1.location(), UniqueId(2));
  global_handles->SetObjectGroupId(g2s2.location(), UniqueId(2));
  global_handles->SetReferenceFromGroup(UniqueId(1), g1c1.location());
  global_handles->SetReferenceFromGroup(UniqueId(1), g1c2.location());
  global_handles->SetReferenceFromGroup(UniqueId(2), g2c1.location());

  List<ImplicitRefGroup*>* implicit_refs =
      global_handles->implicit_ref_groups();
  USE(implicit_refs);
  ASSERT(implicit_refs->length() == 2);
  ASSERT(implicit_refs->at(0)->parent ==
         reinterpret_cast<HeapObject**>(g1s1.location()));
  ASSERT(implicit_refs->at(0)->length == 2);
  ASSERT(implicit_refs->at(0)->children[0] == g1c1.location());
  ASSERT(implicit_refs->at(0)->children[1] == g1c2.location());
  ASSERT(implicit_refs->at(1)->parent ==
         reinterpret_cast<HeapObject**>(g2s1.location()));
  ASSERT(implicit_refs->at(1)->length == 1);
  ASSERT(implicit_refs->at(1)->children[0] == g2c1.location());
}


static const int kBlockSize = 256;


TEST(BlockCollection) {
  v8::V8::Initialize();
  Isolate* isolate = Isolate::Current();
  GlobalHandles* global_handles = isolate->global_handles();
  CHECK_EQ(0, global_handles->block_count());
  CHECK_EQ(0, global_handles->global_handles_count());
  Object* object = isolate->heap()->undefined_value();
  const int kNumberOfBlocks = 5;
  typedef Handle<Object> Block[kBlockSize];
  for (int round = 0; round < 3; round++) {
    Block blocks[kNumberOfBlocks];
    for (int i = 0; i < kNumberOfBlocks; i++) {
      for (int j = 0; j < kBlockSize; j++) {
        blocks[i][j] = global_handles->Create(object);
      }
    }
    CHECK_EQ(kNumberOfBlocks, global_handles->block_count());
    for (int i = 0; i < kNumberOfBlocks; i++) {
      for (int j = 0; j < kBlockSize; j++) {
        global_handles->Destroy(blocks[i][j].location());
      }
    }
    isolate->heap()->CollectAllAvailableGarbage("BlockCollection");
    CHECK_EQ(0, global_handles->global_handles_count());
    CHECK_EQ(1, global_handles->block_count());
  }
}


class RandomMutationData {
 public:
  explicit RandomMutationData(Isolate* isolate)
      : isolate_(isolate), weak_offset_(0) {}

  void Mutate(double strong_growth_tendency,
              double weak_growth_tendency = 0.05) {
    for (int i = 0; i < kBlockSize * 100; i++) {
      if (rng_.next(strong_growth_tendency)) {
        AddStrong();
      } else if (strong_nodes_.size() != 0) {
        size_t to_remove = rng_.next(static_cast<int>(strong_nodes_.size()));
        RemoveStrong(to_remove);
      }
      if (rng_.next(weak_growth_tendency)) AddWeak();
      if (rng_.next(0.05)) {
#ifdef DEBUG
        isolate_->global_handles()->VerifyBlockInvariants();
#endif
      }
      if (rng_.next(0.0001)) {
        isolate_->heap()->PerformScavenge();
      } else if (rng_.next(0.00003)) {
        isolate_->heap()->CollectAllAvailableGarbage();
      }
      CheckSizes();
    }
  }

  void RemoveAll() {
    while (strong_nodes_.size() != 0) {
      RemoveStrong(strong_nodes_.size() - 1);
    }
    isolate_->heap()->PerformScavenge();
    isolate_->heap()->CollectAllAvailableGarbage();
    CheckSizes();
  }

 private:
  typedef std::vector<Object**> NodeVector;
  typedef std::map<int32_t, Object**> NodeMap;

  void CheckSizes() {
    int stored_sizes =
        static_cast<int>(strong_nodes_.size() + weak_nodes_.size());
    CHECK_EQ(isolate_->global_handles()->global_handles_count(), stored_sizes);
  }

  void AddStrong() {
    Object* object = isolate_->heap()->undefined_value();
    Object** location = isolate_->global_handles()->Create(object).location();
    strong_nodes_.push_back(location);
  }

  void RemoveStrong(size_t offset) {
    isolate_->global_handles()->Destroy(strong_nodes_.at(offset));
    strong_nodes_.erase(strong_nodes_.begin() + offset);
  }

  void AddWeak() {
    v8::Isolate* isolate = reinterpret_cast<v8::Isolate*>(isolate_);
    v8::HandleScope scope(isolate);
    v8::Local<v8::Object> object = v8::Object::New();
    int32_t offset = ++weak_offset_;
    object->Set(7, v8::Integer::New(offset, isolate));
    v8::Persistent<v8::Object> persistent(isolate, object);
    persistent.MakeWeak(this, WeakCallback);
    persistent.MarkIndependent();
    Object** location = v8::Utils::OpenPersistent(persistent).location();
    bool inserted =
        weak_nodes_.insert(std::make_pair(offset, location)).second;
    CHECK(inserted);
  }

  static void WeakCallback(v8::Isolate* isolate,
                           v8::Persistent<v8::Object>* persistent,
                           RandomMutationData* data) {
    v8::Local<v8::Object> object =
        v8::Local<v8::Object>::New(isolate, *persistent);
    int32_t offset =
        v8::Local<v8::Integer>::Cast(object->Get(7))->Int32Value();
    Object** location = v8::Utils::OpenPersistent(persistent).location();
    NodeMap& weak_nodes = data->weak_nodes_;
    NodeMap::iterator it = weak_nodes.find(offset);
    CHECK(it != weak_nodes.end());
    CHECK(it->second == location);
    weak_nodes.erase(it);
    persistent->Dispose();
  }

  Isolate* isolate_;
  RandomNumberGenerator rng_;
  NodeVector strong_nodes_;
  NodeMap weak_nodes_;
  int32_t weak_offset_;
};


TEST(RandomMutation) {
  v8::V8::Initialize();
  Isolate* isolate = Isolate::Current();
  CHECK_EQ(0, isolate->global_handles()->block_count());
  HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(
      v8::Context::New(reinterpret_cast<v8::Isolate*>(isolate)));
  RandomMutationData data(isolate);
  // grow some
  data.Mutate(0.65);
  data.Mutate(0.55);
  // balanced mutation
  for (int i = 0; i < 3; i++) data.Mutate(0.50);
  // shrink some
  data.Mutate(0.45);
  data.Mutate(0.35);
  // clear everything
  data.RemoveAll();
}


TEST(EternalHandles) {
  CcTest::InitializeVM();
  Isolate* isolate = Isolate::Current();
  v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(isolate);
  EternalHandles* eternals = isolate->eternal_handles();

  // Create a number of handles that will not be on a block boundary
  const int kArrayLength = 2048-1;
  int indices[kArrayLength];

  CHECK_EQ(0, eternals->NumberOfHandles());
  for (int i = 0; i < kArrayLength; i++) {
    HandleScope scope(isolate);
    v8::Local<v8::Object> object = v8::Object::New();
    object->Set(i, v8::Integer::New(i, v8_isolate));
    if (i % 2 == 0) {
      // Create with internal api
      indices[i] = eternals->Create(isolate, *v8::Utils::OpenHandle(*object));
    } else {
      // Create with external api
      indices[i] = object.Eternalize(v8_isolate);
    }
  }

  isolate->heap()->CollectAllAvailableGarbage();

  for (int i = 0; i < kArrayLength; i++) {
    for (int j = 0; j < 2; j++) {
      HandleScope scope(isolate);
      v8::Local<v8::Object> object;
      if (j == 0) {
        // Test internal api
        v8::Local<v8::Value> local =
            v8::Utils::ToLocal(eternals->Get(indices[i]));
        object = v8::Handle<v8::Object>::Cast(local);
      } else {
        // Test external api
        object = v8::Local<v8::Object>::GetEternal(v8_isolate, indices[i]);
      }
      v8::Local<v8::Value> value = object->Get(i);
      CHECK(value->IsInt32());
      CHECK_EQ(i, value->Int32Value());
    }
  }

  CHECK_EQ(kArrayLength, eternals->NumberOfHandles());
}

