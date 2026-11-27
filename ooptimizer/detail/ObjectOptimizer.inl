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

    /*!
     * @brief 列挙値または整数の処理IDを実行プランのキーへ変換する。
     * @tparam Operation 処理IDの列挙型またはbool以外の整数型。
     * @param[in] operation 変換する処理ID。
     * @return 変換後の数値キー。
     */
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

    /*!
    *   @brief OperationBindingを識別するための基底型。
    */
    struct OperationBindingTag {};

} // namespace ooptimizer::detail

namespace ooptimizer {

    template<auto OperationValue, auto MethodValue> 
    struct OperationBinding : detail::OperationBindingTag {
        /*!
        *   @brief 処理IDの型。
        */
        using OperationType = std::remove_cv_t<decltype(OperationValue)>;

        static_assert(std::is_enum_v<OperationType> || (std::is_integral_v<OperationType> && !std::is_same_v<OperationType, bool>),
                      "OperationBindingの処理IDには列挙値またはbool以外の整数を指定する必要があります。");

        /*!
        *   @brief 登録する処理ID。
        */
        static constexpr auto Value = OperationValue;

        /*!
        *   @brief オブジェクトごとに呼び出す処理。
        */
        static constexpr auto Method = MethodValue;
    };

} // namespace ooptimizer

namespace ooptimizer::detail {

    // 処理定義の検証

    /*!
    *   @brief TがOperationBindingの識別用基底型を持つかを表す。
    */
    template<class T> 
    inline constexpr bool kIsOperationBinding = std::is_base_of_v<OperationBindingTag, T>;

    /*!
     * @brief 実行プラン用の数値キーに変換して処理IDの重複を調べる。
     * @tparam Bindings 検証する処理定義。
     * @return キーが重複する場合はfalse、それ以外はtrue。
     * @note 不正な型が含まれる場合はtrueを返し、型の検証はOperationSetに委ねる。
     */
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

    /*!
    *   @brief TがOperationSetであるかを表す型判定。
    */
    template<class T> 
    inline constexpr bool kIsOperationSet = false;

    /*!
    *   @brief OperationSetの実体化を通じて、処理定義の型とIDを検証する。
    */
    template<class... Bindings>
    inline constexpr bool kIsOperationSet<OperationSet<Bindings...>> = sizeof(OperationSet<Bindings...>) > 0;

    /*!
    *   @brief OperationSetに含まれる処理を登録するための内部型。
    */
    template<class OperationSetType> 
    struct OperationSetBinder;

    template<class... Bindings> 
    struct OperationSetBinder<OperationSet<Bindings...>> {
        /*!
         * @brief 各処理定義のIDと呼び出す処理を登録先へ渡す。
         * @tparam T 登録する具体型。
         * @tparam Registration Tの処理を登録する内部型。
         * @param[in,out] registration Tの処理の登録先。
         */
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

    /*!
     * @brief RTTIを使わずに具体型を識別するキーを取得する。
     * @tparam T 識別する型。
     * @return Tの識別用変数のアドレス。
     */
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
        /*!
        *   @brief 内部インターフェースを通して具体型の格納領域を破棄する。
        */
        virtual ~IStorage() = default;

        /*!
         * @brief 実行優先度を更新し、初回登録時に登録順を記録する。
         * @param[in] priority 新しい実行優先度。
         * @param[in] registrationOrder 初回登録時に使う登録順の番号。
         * @return 初回登録順を記録した場合はtrue、それ以外はfalse。
         */
        [[nodiscard]]
        bool UpdateExecutionOrder(std::int32_t priority, std::size_t registrationOrder) noexcept {
            executionPriority_ = priority;

            if (hasExecutionOrder_) return false;

            firstRegistrationOrder_ = registrationOrder;
            hasExecutionOrder_ = true;
            return true;
        }

        /*!
        *   @return 現在の実行優先度。
        */
        [[nodiscard]]
        inline std::int32_t ExecutionPriority() const noexcept {
            return executionPriority_;
        }

        /*!
        *   @return 優先度が同じときの比較に使う初回登録順。
        */
        [[nodiscard]]
        inline std::size_t FirstRegistrationOrder() const noexcept {
            return firstRegistrationOrder_;
        }

        /*!
        *   @return 実行対象になっているオブジェクト数。
        */
        [[nodiscard]]
        virtual std::size_t Size() const noexcept = 0;

        /*!
        *   @return 実行対象への追加を待っているオブジェクト数。
        */
        [[nodiscard]]
        virtual std::size_t PendingSize() const noexcept = 0;


        /*!
        *   @brief この格納領域の削除要求を反映する。
        */
        virtual void FlushDestroyQueue() noexcept = 0;

        /*!
        *   @brief この格納領域の保留オブジェクトを実行対象に加える。
        */
        virtual void MergePending() noexcept = 0;

    private:
        std::int32_t executionPriority_ = 0;

        std::size_t firstRegistrationOrder_ = 0;

        bool hasExecutionOrder_ = false;
    };

    template<class Base> 
    struct ObjectOptimizer<Base>::TypeEntry {
        /*!
        *   @brief 格納している具体型の識別キー。
        */
        const void* typeKey = nullptr;

        /*!
        *   @brief この登録情報が所有する型別格納領域。
        */
        std::unique_ptr<IStorage> storage;
    };

    template<class Base> 
    struct ObjectOptimizer<Base>::PlanNode {
        using Runner = void (*)(IStorage*) noexcept;

        /*!
        *   @brief 実行プラン内で具体型を照合するキー。
        */
        const void* typeKey = nullptr;

        /*!
        *   @brief 処理対象となる格納領域への非所有ポインタ。
        */
        IStorage* storage = nullptr;

        /*!
        *   @brief 対応する具体型の稼働中オブジェクトを処理する関数。
        */
        Runner runner = nullptr;
    };

    template<class Base> template<class T> 
    class ObjectOptimizer<Base>::RegistrationBuilder {
    public:
        /*!
         * @brief 具体型Tの処理を登録するための内部オブジェクトを構築する。
         * @param[in,out] owner 登録先のObjectOptimizer。
         * @param[in] storage Tの格納領域。
         */
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
        /*!
        *   @brief 1ブロックに格納できるオブジェクト数。
        */
        static constexpr std::size_t SlotsPerPage{std::clamp<std::size_t>(4096 / sizeof(T), 1, 64)};
        /*!
        *   @brief ブロック内の有効なスロットに対応するビットマスク。
        */
        static constexpr std::uint64_t FullPageMask{~std::uint64_t{} >> (64 - SlotsPerPage)};

        struct Page;

        /*!
        *   @brief 削除対象を格納ブロックとスロットのビットで指定する。
        */
        struct DestroyRequest {
            Page* page;
            std::uint64_t bit;
        };

        /*!
        *   @brief Tの固定長格納ブロック。所属するオブジェクトの削除要求も受け付ける。
        */
        struct Page final : detail::IDestroyQueue {
            /*!
             * @name 構築と破棄
             *
             * 所有元のStorageに属する空のブロックを構築する。
             * ブロックの破棄時に、残存するオブジェクトを破棄する。
             * @{
             */
            explicit Page(Storage& storage) noexcept : owner{storage} {}

            ~Page() noexcept override {
                for (std::uint64_t mask{occupied}; mask != 0; mask &= mask - 1)
                    std::destroy_at(std::addressof(Object(std::countr_zero(mask))));
            }
            /// @}

            /*!
             * @brief スロットの格納アドレスを取得する。
             * @param[in] index ブロック内のスロット番号。
             * @return Tの格納先アドレス。
             * @pre indexがブロック内の範囲にあること。
             */
            T* Address(std::size_t index) noexcept {
                return reinterpret_cast<T*>(data + index * sizeof(T));
            }

            /*!
             * @brief スロットに構築済みのオブジェクトを取得する。
             * @param[in] index ブロック内のスロット番号。
             * @return スロットにあるTへの参照。
             * @pre 指定スロットにTが構築されていること。
             */
            T& Object(std::size_t index) noexcept {
                return *std::launder(Address(index));
            }

            /*!
             * @brief このブロックのオブジェクトの削除要求を所有元のStorageへ登録する。
             * @param[in] object 削除するオブジェクト。
             * @pre objectがこのブロックに所属していること。
             */
            void QueueDestroy(OptimizedObject* object) noexcept override {
                const std::uint64_t bit{ObjectBit(object)};
                assert(bit != 0 && "このブロックに所属しないオブジェクトです。");
                if (bit == 0 || (destroyRequested & bit) != 0)
                    return;

                owner.destroyQueue_.push_back(DestroyRequest{this, bit});
                destroyRequested |= bit;
                owner.MarkDestroyDirty();
            }

            /*!
             * @brief 指定オブジェクトの削除要求が登録済みか調べる。
             * @param[in] object 確認するオブジェクト。
             * @return 対応するスロットに削除要求があればtrue。
             */
            bool IsDestroyRequested(const OptimizedObject* object) const noexcept override {
                return (ObjectBit(object) & destroyRequested) != 0;
            }

            Storage& owner;
            /*!
            *   @brief 構築済みの全スロットを表すビットマスク。
            */
            std::uint64_t occupied{};
            /*!
            *   @brief 実行対象のスロットを表すビットマスク。
            */
            std::uint64_t active{};
            /*!
            *   @brief 削除要求が登録済みのスロットを表すビットマスク。
            */
            std::uint64_t destroyRequested{};

        private:
            /*!
             * @brief オブジェクトの格納位置をスロットのビットに変換する。
             * @param[in] object 位置を調べるオブジェクト。
             * @return 対応する占有スロットのビット。該当なしの場合は0。
             */
            std::uint64_t ObjectBit(const OptimizedObject* object) const noexcept {
                const std::uintptr_t begin{reinterpret_cast<std::uintptr_t>(data)};
                const std::uintptr_t address{
                    reinterpret_cast<std::uintptr_t>(static_cast<const T*>(object))};
                const std::uintptr_t offset{address - begin};

                if (address < begin || offset >= sizeof(data) || offset % sizeof(T) != 0) return 0;

                return (std::uint64_t{1} << (offset / sizeof(T))) & occupied;
            }

            alignas(T) std::byte data[SlotsPerPage * sizeof(T)];
        };

    public:
        /*!
         * @brief 具体型Tの空の格納領域を構築する。
         * @param[in,out] owner 格納領域を所有し、削除・保留処理を管理するObjectOptimizer。
         */
        explicit Storage(ObjectOptimizer& owner) noexcept : owner_{owner} {}

        /*!
         * @brief 空きスロットにTを直接構築し、削除要求の送信先を設定する。
         * @tparam Args Tのコンストラクタ引数の型。
         * @param[in] deferred trueの場合は保留状態で、falseの場合は稼働状態で生成する。
         * @param[in] args Tのコンストラクタへ転送する引数。
         * @return 生成したオブジェクトへの参照。
         */
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

        /*!
         * @brief 指定された要素数を格納できる分のブロックを事前確保する。
         * @param[in] count 稼働中と保留中を合わせた格納容量の下限。
         */
        void Reserve(std::size_t count) noexcept {
            const std::size_t required{count / SlotsPerPage + (count % SlotsPerPage != 0)};

            pages_.reserve(required);
            recycledPages_.reserve(required);

            while (pages_.size() < required)pages_.push_back(std::make_unique<Page>(*this));
        }

        /*!
        *   @return この型の稼働中のオブジェクト数。
        */
        [[nodiscard]] std::size_t Size() const noexcept override { return activeCount_; }
        /*!
        *   @return この型の保留中のオブジェクト数。
        */
        [[nodiscard]] std::size_t PendingSize() const noexcept override { return pendingCount_; }

        /*!
         * @brief この型の稼働中の各オブジェクトに処理を適用する。
         * @tparam Method T&を受け取って呼び出せる処理。
         */
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

        /*!
        *   @brief 削除要求を反映し、対象オブジェクトのスロットを再利用できる状態にする。
        */
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

        /*!
        *   @brief 保留オブジェクトを現在の格納位置のまま実行対象に加える。
        */
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
        /*!
        *   @brief 次回の削除処理の対象として、この格納領域を一度だけ登録する。
        */
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
