#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <utility>

namespace ooptimizer::detail {

    // 処理IDの内部表現

    template<class Operation> 
    constexpr std::uint64_t ToOperationKey(Operation operation) noexcept {
        using Value = std::remove_cv_t<Operation>;

        static_assert(std::is_enum_v<Value> || (std::is_integral_v<Value> && !std::is_same_v<Value, bool>),
                      "処理IDには列挙値またはbool以外の整数を指定する必要があります。");

        if constexpr (std::is_enum_v<Value>) {
            using Underlying = std::underlying_type_t<Value>;

            return static_cast<std::uint64_t>(static_cast<Underlying>(operation));
        } else {
            return static_cast<std::uint64_t>(operation);
        }
    }

    struct OperationBindingTag {};

} // namespace ooptimizer::detail

namespace ooptimizer {

    template<auto OperationValue, auto MethodValue> 
    struct OperationBinding : detail::OperationBindingTag {
        using OperationType = std::remove_cv_t<decltype(OperationValue)>;

        static_assert(std::is_enum_v<OperationType> || (std::is_integral_v<OperationType> && !std::is_same_v<OperationType, bool>),
                      "OperationBindingの処理IDには列挙値またはbool以外の整数を指定する必要があります。");

        static constexpr auto Value = OperationValue;

        static constexpr auto Method = MethodValue;
    };

} // namespace ooptimizer

namespace ooptimizer::detail {

    // 処理定義の検証

    template<class T> 
    inline constexpr bool kIsOperationBinding = std::is_base_of_v<OperationBindingTag, T>;

    template<class... Bindings> 
    consteval bool HasUniqueOperationIds() noexcept {
        if constexpr (!(kIsOperationBinding<Bindings> && ...)) {
            return true;
        } else {
            constexpr std::array<std::uint64_t, sizeof...(Bindings)> ids{ToOperationKey(Bindings::Value)...};

            for (std::size_t i = 0; i < ids.size(); ++i) {
                for (std::size_t j = i + 1; j < ids.size(); ++j) {
                    if (ids[i] == ids[j]) {
                        return false;
                    }
                }
            }

            return true;
        }
    }

} // namespace ooptimizer::detail

namespace ooptimizer {

    template<class... Bindings> 
    struct OperationSet {
        static_assert((detail::kIsOperationBinding<Bindings> && ...), "OperationSetにはooptimizer::OperationBindingだけを指定する必要があります。");

        static_assert(detail::HasUniqueOperationIds<Bindings...>(), "OperationSet内の処理IDが重複しています。");
    };

} // namespace ooptimizer

namespace ooptimizer::detail {

    template<class T> 
    inline constexpr bool kIsOperationSet = false;

    template<class... Bindings>
    inline constexpr bool kIsOperationSet<OperationSet<Bindings...>> = sizeof(OperationSet<Bindings...>) > 0;

    template<class OperationSetType> 
    struct OperationSetBinder;

    template<class... Bindings> 
    struct OperationSetBinder<OperationSet<Bindings...>> {
        template<class T, class Registration> 
        static void Bind(Registration& registration) noexcept {
            (registration.template Bind<Bindings::Value, Bindings::Method>(), ...);
        }
    };

    // 型識別

    /**
     * @brief RTTIを使わない型識別用キー。
     */
    template<class T> 
    inline constexpr std::uint8_t kTypeKey = 0;

    template<class T> 
    const void* TypeKey() noexcept {
        return &kTypeKey<T>;
    }

} // namespace ooptimizer::detail

namespace ooptimizer {

    // 内部型

    template<class Base> 
    class ObjectOptimizer<Base>::IStorage {
    public:
        virtual ~IStorage() = default;

        [[nodiscard]]
        bool UpdateExecutionOrder(std::int32_t priority, std::size_t registrationOrder) noexcept {
            executionPriority_ = priority;

            if (hasExecutionOrder_) return false;

            firstRegistrationOrder_ = registrationOrder;
            hasExecutionOrder_ = true;
            return true;
        }

        [[nodiscard]]
        std::int32_t ExecutionPriority() const noexcept {
            return executionPriority_;
        }

        [[nodiscard]]
        std::size_t FirstRegistrationOrder() const noexcept {
            return firstRegistrationOrder_;
        }

        [[nodiscard]]
        virtual std::size_t Size() const noexcept = 0;

        [[nodiscard]]
        virtual std::size_t PendingSize() const noexcept = 0;


        virtual void FlushDestroyQueue() noexcept = 0;

        virtual void MergePending() noexcept = 0;

    private:
        std::int32_t executionPriority_ = 0;

        std::size_t firstRegistrationOrder_ = 0;

        bool hasExecutionOrder_ = false;
    };

    template<class Base> 
    struct ObjectOptimizer<Base>::TypeEntry {
        const void* typeKey = nullptr;

        std::unique_ptr<IStorage> storage;
    };

    template<class Base> 
    struct ObjectOptimizer<Base>::PlanNode {
        const void* typeKey = nullptr;

        IStorage* storage = nullptr;

        void (*runner)(IStorage* storage) noexcept = nullptr;
    };

    template<class Base> template<class T> 
    class ObjectOptimizer<Base>::RegistrationBuilder {
    public:
        RegistrationBuilder(ObjectOptimizer& owner, Storage<T>& storage) noexcept;

        /*!
         * @brief 処理IDとTに対して呼び出す処理を関連付ける。
         * @tparam OperationValue 関連付ける列挙値または整数の処理ID。
         * @tparam Method T&で呼び出し可能な値。
         * @return この登録結果への参照。
         *
         * @note 同じ処理IDとTへ再度Bindした場合は、以前の関連付けを上書きする。
         * @note 関連付けに必要なメモリを確保できない場合はstd::terminate()を呼び出す。
        */
        template<auto OperationValue, auto Method> RegistrationBuilder& Bind() noexcept;

    private:
        ObjectOptimizer* owner_ = nullptr;

        Storage<T>* storage_ = nullptr;
    };

    // 型別格納領域
    template<class Base>
    template<class T>
    class ObjectOptimizer<Base>::Storage final : public ObjectOptimizer<Base>::IStorage {
        static constexpr std::size_t SlotsPerPage{std::clamp<std::size_t>(4096 / sizeof(T), 1, 64)};
        static constexpr std::uint64_t FullPageMask{~std::uint64_t{} >> (64 - SlotsPerPage)};

        struct Page;

        struct DestroyRequest {
            Page* page;
            std::uint64_t bit;
        };

        struct Page final : detail::IDestroyQueue {
            explicit Page(Storage& storage) noexcept : owner{storage} {}

            ~Page() noexcept override {
                for (std::uint64_t mask{occupied}; mask != 0; mask &= mask - 1)
                    std::destroy_at(std::addressof(Object(std::countr_zero(mask))));
            }

            T* Address(std::size_t index) noexcept {
                return reinterpret_cast<T*>(data + index * sizeof(T));
            }

            T& Object(std::size_t index) noexcept {
                return *std::launder(Address(index));
            }

            void QueueDestroy(OptimizedObject* object) noexcept override {
                const std::uint64_t bit{ObjectBit(object)};
                assert(bit != 0 && "このブロックに所属しないオブジェクトです。");
                if (bit == 0 || (destroyRequested & bit) != 0)
                    return;

                owner.destroyQueue_.push_back(DestroyRequest{this, bit});
                destroyRequested |= bit;
                owner.MarkDestroyDirty();
            }

            bool IsDestroyRequested(const OptimizedObject* object) const noexcept override {
                return (ObjectBit(object) & destroyRequested) != 0;
            }

            Storage& owner;
            std::uint64_t occupied{};
            std::uint64_t active{};
            std::uint64_t destroyRequested{};

        private:
            std::uint64_t ObjectBit(const OptimizedObject* object) const noexcept {
                const std::uintptr_t begin{reinterpret_cast<std::uintptr_t>(data)};
                const std::uintptr_t address{
                    reinterpret_cast<std::uintptr_t>(static_cast<const T*>(object))};
                const std::uintptr_t offset{address - begin};

                if (address < begin || offset >= sizeof(data) || offset % sizeof(T) != 0)
                    return 0;

                return (std::uint64_t{1} << (offset / sizeof(T))) & occupied;
            }

            alignas(T) std::byte data[SlotsPerPage * sizeof(T)];
        };

    public:
        explicit Storage(ObjectOptimizer& owner) noexcept : owner_{owner} {}

        template<class... Args>
        T& Emplace(bool deferred, Args&&... args) noexcept {
            const bool reusingPage{!recycledPages_.empty()};
            if (!reusingPage && nextPage_ == pages_.size()) pages_.push_back(std::make_unique<Page>(*this));

            Page* page{};
            if (reusingPage) page = recycledPages_.back();
            else page = pages_[nextPage_].get();

            const std::size_t index{static_cast<std::size_t>(std::countr_zero(FullPageMask & ~page->occupied))};
            const std::uint64_t bit{std::uint64_t{1} << index};

            page->occupied |= bit;
            if (page->occupied == FullPageMask) {
                if (reusingPage) recycledPages_.pop_back();
                else ++nextPage_;
            }

            T& object{*std::construct_at(page->Address(index), std::forward<Args>(args)...)};
            ObjectOptimizer::BindManagedObject(object, page);

            if (deferred) {
                ++pendingCount_;
                if (!pendingDirtyRegistered_) {
                    owner_.RegisterPendingDirty(this);
                    pendingDirtyRegistered_ = true;
                }
            } else {
                ++activeCount_;
                page->active |= bit;
            }

            return object;
        }

        void Reserve(std::size_t count) noexcept {
            const std::size_t required{count / SlotsPerPage + (count % SlotsPerPage != 0)};

            pages_.reserve(required);
            recycledPages_.reserve(required);

            while (pages_.size() < required)pages_.push_back(std::make_unique<Page>(*this));
        }

        [[nodiscard]] std::size_t Size() const noexcept override { return activeCount_; }
        [[nodiscard]] std::size_t PendingSize() const noexcept override { return pendingCount_; }

        template<auto Method>
        void ForEachActive() noexcept {
            // コールバックはブロックを追加できるため、ポインタ配列の反復子を保持しない。
            const std::size_t pageCount{std::min(nextPage_ + 1, pages_.size())};
            for (std::size_t pageIndex{}; pageIndex < pageCount; ++pageIndex) {
                Page& page{*pages_[pageIndex]};
                std::uint64_t mask{page.active};

                // 先頭から詰まったブロックは、各スロットの占有判定をせずに走査する。
                if ((mask & (mask + 1)) == 0) {
                    const std::size_t count{static_cast<std::size_t>(std::countr_one(mask))};

                    for (std::size_t index{}; index < count; ++index) {
                        std::invoke(Method, page.Object(index));
                    }
                    continue;
                }

                for (; mask != 0; mask &= mask - 1) {
                    std::invoke(Method, page.Object(std::countr_zero(mask)));
                }
            }
        }

        void FlushDestroyQueue() noexcept override {
            for (std::size_t i{}; i < destroyQueue_.size(); ++i) {
                const DestroyRequest request{destroyQueue_[i]};
                Page& page{*request.page};
                const std::uint64_t bit{request.bit};

                std::destroy_at(std::addressof(page.Object(std::countr_zero(bit))));
                if (page.occupied == FullPageMask)
                    recycledPages_.push_back(&page);

                if ((page.active & bit) != 0) --activeCount_;
                else --pendingCount_;

                page.occupied &= ~bit;
                page.active &= ~bit;
                page.destroyRequested &= ~bit;
            }

            destroyQueue_.clear();
            destroyDirtyRegistered_ = false;
        }

        void MergePending() noexcept override {
            if (pendingCount_ != 0) {
                const std::size_t pageCount{std::min(nextPage_ + 1, pages_.size())};

                for (std::size_t i{}; i < pageCount; ++i) {
                    pages_[i]->active = pages_[i]->occupied;
                }

                activeCount_ += pendingCount_;
                pendingCount_ = 0;
            }
            pendingDirtyRegistered_ = false;
        }

    private:
        void MarkDestroyDirty() noexcept {
            if (!destroyDirtyRegistered_) {
                owner_.RegisterDestroyDirty(this);
                destroyDirtyRegistered_ = true;
            }
        }

        ObjectOptimizer& owner_;
        std::vector<std::unique_ptr<Page>> pages_;
        std::vector<Page*> recycledPages_;
        std::vector<DestroyRequest> destroyQueue_;
        std::size_t nextPage_{};
        std::size_t activeCount_{};
        std::size_t pendingCount_{};
        bool destroyDirtyRegistered_{};
        bool pendingDirtyRegistered_{};
    };

    // ObjectOptimizerの生存期間

    template<class Base> 
    ObjectOptimizer<Base>::ObjectOptimizer() noexcept = default;

    template<class Base> 
    ObjectOptimizer<Base>::~ObjectOptimizer() noexcept = default;

    template<class Base>
    void ObjectOptimizer<Base>::BindManagedObject(OptimizedObject& object, detail::IDestroyQueue* queue) noexcept {
        object.BindDestroyQueue(queue);
    }

    // 登録ビルダー

    template<class Base>
    template<class T>
    ObjectOptimizer<Base>::RegistrationBuilder<T>::RegistrationBuilder(ObjectOptimizer& owner,Storage<T>& storage) noexcept
        :owner_(&owner),storage_(&storage) {}

    template<class Base>
    template<class T>
    template<auto OperationValue, auto Method>
    auto ObjectOptimizer<Base>::RegistrationBuilder<T>::Bind() noexcept -> RegistrationBuilder& {
        static_assert(std::is_invocable_v<decltype((Method)), T&>, "関連付けた処理はT&を使用して呼び出せる必要があります。");

        owner_->template BindImpl<T, OperationValue, Method>(*storage_);

        return *this;
    }

    // 登録と生成

    template<class Base> template<class T> 
    void ObjectOptimizer<Base>::Register(std::size_t reserveCount, std::int32_t priority) noexcept {
        ValidateType<T>();

        assert(!isExecuting_ && "Execute()中にRegister()を呼び出すことはできません。");

        Storage<T>& storage = EnsureStorage<T>();

        if (storage.UpdateExecutionOrder(priority, nextRegistrationOrder_)) ++nextRegistrationOrder_;

        if (reserveCount > 0) storage.Reserve(reserveCount);

        RegistrationBuilder<T> registration(*this, storage);

        using Operations = typename Base::OOptimizerOperations;

        static_assert(detail::kIsOperationSet<Operations>, "Base::OOptimizerOperationsにはooptimizer::OperationSetを指定する必要があります。");

        if constexpr (detail::kIsOperationSet<Operations>) detail::OperationSetBinder<Operations>::template Bind<T>(registration);
    }

    template<class Base> template<class T, class... Args> 
    T& ObjectOptimizer<Base>::Create(Args&&... args) noexcept {
        ValidateType<T>();

        const void* key = detail::TypeKey<T>();

        assert((!isExecuting_ || typeIndex_.find(key) != typeIndex_.end()) &&
               "Execute()中にTを生成する前に"
               "Register<T>()を呼び出す必要があります。");

        Storage<T>& storage = EnsureStorage<T>();

        return storage.Emplace(isExecuting_, std::forward<Args>(args)...);
    }

    // 実行

    template<class Base> template<class Operation> 
    void ObjectOptimizer<Base>::Execute(Operation operation) noexcept {
        assert(!isExecuting_ && "Execute()を入れ子で呼び出すことはできません。");

        FlushDestroyQueues();

        const std::uint64_t operationKey = detail::ToOperationKey(operation);

        const auto found = executionPlans_.find(operationKey);

        if (found == executionPlans_.end()) {
            MergePending();

            return;
        }

        isExecuting_ = true;

        for (const PlanNode& node : found->second) {
            node.runner(node.storage);
        }

        isExecuting_ = false;

        FlushDestroyQueues();

        MergePending();
    }

    template<class Base> 
    void ObjectOptimizer<Base>::FlushDestroyed() noexcept {
        assert(!isExecuting_ && "Execute()中にFlushDestroyed()を"
                                "呼び出すことはできません。");

        FlushDestroyQueues();

        MergePending();
    }

    // 情報取得

    template<class Base> 
    std::size_t ObjectOptimizer<Base>::Size() const noexcept {
        std::size_t result = 0;

        for (const TypeEntry& entry : types_) {
            result += entry.storage->Size();

            result += entry.storage->PendingSize();
        }

        return result;
    }

    template<class Base> template<class T> 
    std::size_t ObjectOptimizer<Base>::Count() const noexcept {
        const auto found = typeIndex_.find(detail::TypeKey<T>());

        if (found == typeIndex_.end()) return 0;

        const Storage<T>* storage = static_cast<const Storage<T>*>(types_[found->second].storage.get());

        return storage->Size() + storage->PendingSize();
    }

    // 型検証と格納領域

    template<class Base> template<class T> 
    consteval void ObjectOptimizer<Base>::ValidateType() {
        static_assert(std::is_base_of_v<Base, T>, "TはBaseを継承する必要があります。");

        static_assert(!std::is_abstract_v<T>, "Tには具象型を指定する必要があります。");

        static_assert(std::is_destructible_v<T>, "Tは破棄可能である必要があります。");
    }

    template<class Base> template<class T> 
    auto ObjectOptimizer<Base>::EnsureStorage() noexcept -> Storage<T>& {
        const void* key = detail::TypeKey<T>();

        const auto found = typeIndex_.find(key);

        if (found != typeIndex_.end()) return *static_cast<Storage<T>*>(types_[found->second].storage.get());

        std::unique_ptr<Storage<T>> storage = std::make_unique<Storage<T>>(*this);

        Storage<T>* storagePointer = storage.get();

        TypeEntry entry;

        entry.typeKey = key;

        entry.storage = std::move(storage);

        const std::size_t index = types_.size();

        types_.push_back(std::move(entry));

        typeIndex_.emplace(key, index);

        return *storagePointer;
    }

    // 関連付けと実行関数

    template<class Base>
    template<class T, auto OperationValue, auto Method>
    void ObjectOptimizer<Base>::BindImpl(Storage<T>& storage) noexcept {
        const void* typeKey = detail::TypeKey<T>();

        std::vector<PlanNode>& plan = executionPlans_[detail::ToOperationKey(OperationValue)];

        const auto compare = [](const PlanNode& left, const PlanNode& right) noexcept {
            const std::int32_t leftPriority = left.storage->ExecutionPriority();
            const std::int32_t rightPriority = right.storage->ExecutionPriority();

            if (leftPriority != rightPriority) return leftPriority < rightPriority;

            return left.storage->FirstRegistrationOrder() < right.storage->FirstRegistrationOrder();
        };

        for (PlanNode& node : plan) {
            if (node.typeKey == typeKey) {
                node.storage = &storage;

                node.runner = &ExecuteStorage<T, Method>;

                std::sort(plan.begin(), plan.end(), compare);

                return;
            }
        }

        PlanNode node;

        node.typeKey = typeKey;

        node.storage = &storage;

        node.runner = &ExecuteStorage<T, Method>;

        plan.push_back(node);

        std::sort(plan.begin(), plan.end(), compare);
    }

    template<class Base>
    template<class T, auto Method>
    void ObjectOptimizer<Base>::ExecuteStorage(IStorage* storageAddress) noexcept {
        static_cast<Storage<T>*>(storageAddress)->template ForEachActive<Method>();
    }

    // 変更された格納領域の処理

    template<class Base> 
    void ObjectOptimizer<Base>::RegisterDestroyDirty(IStorage* storage) noexcept {
        destroyDirtyStorages_.push_back(storage);
    }

    template<class Base> 
    void ObjectOptimizer<Base>::RegisterPendingDirty(IStorage* storage) noexcept {
        pendingDirtyStorages_.push_back(storage);
    }

    template<class Base> 
    void ObjectOptimizer<Base>::FlushDestroyQueues() noexcept {
        std::size_t processed = 0;

        while (processed < destroyDirtyStorages_.size()) {
            destroyDirtyStorages_[processed]->FlushDestroyQueue();

            ++processed;
        }

        destroyDirtyStorages_.clear();
    }

    template<class Base> 
    void ObjectOptimizer<Base>::MergePending() noexcept {
        std::size_t processed = 0;

        while (processed < pendingDirtyStorages_.size()) {
            pendingDirtyStorages_[processed]->MergePending();

            ++processed;
        }

        pendingDirtyStorages_.clear();
    }

} // namespace ooptimizer
