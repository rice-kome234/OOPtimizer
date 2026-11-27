#pragma once

#include "OptimizedObject.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace ooptimizer {

    /*!
     * @brief 処理IDと各オブジェクトに対して呼び出す処理を関連付ける。
     * @tparam OperationValue 関連付ける列挙値または整数の処理ID。
     * @tparam MethodValue メンバ関数ポインタ、またはキャプチャなしラムダなどの呼び出し可能な値。
     * @details メンバ関数ポインタは従来どおり呼び出す。仮想呼び出しを省く場合は、
     *          ラムダ内で `object.T::Method()` のように具体型で修飾して呼び出す。
     */
    template<auto OperationValue, auto MethodValue> 
    struct OperationBinding;

    /*!
     * @brief 基底型が対応する処理の関連付けをまとめる。
     * @tparam Bindings OperationBindingで定義した処理の関連付け。
     *
     * 基底型の公開型OOptimizerOperationsへ指定して使用する。
     * @note 数値として同じ処理IDを複数指定することはできない。
     *
     * @code
     * enum class GameOperation {
     *     Update,
     *     Checksum
     * };
     *
     * class GameObject : public ooptimizer::OptimizedObject {
     * public:
     *     virtual void Update() = 0;
     *     virtual void CollectChecksum() const noexcept = 0;
     *
     *     using OOptimizerOperations =
     *         ooptimizer::OperationSet<
     *             ooptimizer::OperationBinding<GameOperation::Update,[]<class T>(T& object) { object.T::Update(); }>,
     *             ooptimizer::OperationBinding<GameOperation::Checksum, &GameObject::CollectChecksum>
     *         >;
     * };
     * @endcode
     */
    template<class... Bindings> 
    struct OperationSet;

    /*!
     * @brief 型別の固定長ブロックへオブジェクトを格納し、処理単位で実行する。
     * @tparam Base 管理する全オブジェクトに共通する基底型。
     *
     * @note BaseはOptimizedObjectを継承している必要がある。
     * @note 同一インスタンスへの操作はスレッドセーフではない。
     * @note @pre違反はデバッグビルドのassertで検出し、リリースビルドでの動作は保証しない。
     * @note 状態変更中に例外が送出された場合は回復せず、std::terminate()を呼び出す。
     */
    template<class Base> 
    class ObjectOptimizer {
        static_assert(std::is_base_of_v<OptimizedObject, Base>,
                      "Baseはooptimizer::OptimizedObjectを継承する必要があります。");

        static_assert(
            requires { typename Base::OOptimizerOperations; },
            "Baseには公開されたOOptimizerOperationsを定義する必要があります。");

    public:
        /*!
         * @name 構築と破棄
         *
         * 空の状態で構築し、破棄時に管理中の全オブジェクトと格納領域を破棄する。
         * コピーとムーブはできない。
         * @{
         */
        ObjectOptimizer() noexcept;

        ~ObjectOptimizer() noexcept;

        ObjectOptimizer(const ObjectOptimizer&) = delete;

        ObjectOptimizer& operator=(const ObjectOptimizer&) = delete;
        /// @}

        /*!
         * @brief 具体型を登録し、必要に応じて領域を事前確保する。
         * @tparam T 登録する具体型。
         * @param[in] reserveCount 稼働領域へ事前確保する要素数。0の場合は事前確保しない。
         * @param[in] priority Tの実行優先度。数値が小さい型から実行する。
         *
         * @pre Execute中ではないこと。
         * @note Base::OOptimizerOperationsの全処理をTへ自動的に関連付ける。
         * @note 優先度が同じ型は、初めてRegisterした順番で実行する。
         * @note 登録済みのTを再度Registerした場合は、実行優先度を更新する。
         * @note 格納領域または事前確保に必要なメモリを確保できない場合はstd::terminate()を呼び出す。
         *
         * @code
         * optimizer.Register<Enemy>(1000, 20);
         * optimizer.Register<Bullet>(1000, 10); // Bulletを先に実行する
         * @endcode
         */
        template<class T> 
        void Register(std::size_t reserveCount = 0, std::int32_t priority = 0) noexcept;

        /*!
         * @brief 指定した具体型のオブジェクトを生成する。
         * @tparam T 生成する具体型。
         * @tparam Args Tのコンストラクタへ転送する引数の型。
         * @param[in] args Tのコンストラクタへ転送する引数。
         * @return 生成したオブジェクトへの参照。対象が実際に破棄されるまで有効。
         *
         * @pre Execute中に生成する場合は、事前にTをRegisterしていること。
         * @note 容量の追加、他の要素の削除、実行対象への追加で参照とポインタは無効にならない。
         * @note 対象の削除が反映された後やObjectOptimizerの破棄後は参照とポインタを使用できない。
         * @note メモリ確保またはTの構築中に例外が送出された場合はstd::terminate()を呼び出す。
         */
        template<class T, class... Args> 
        T& Create(Args&&... args) noexcept;

        /*!
         * @brief 処理IDへ関連付けられた全具体型の処理を実行する。
         * @tparam Operation 処理IDとして使用する列挙型または整数型。
         * @param[in] operation 実行する処理ID。
         *
         * @details 実行前後に削除待ちキューを処理し、実行中に生成された保留オブジェクトは
         *          フェーズ終了後に移動せず実行対象へ追加する。
         *
         * @pre Execute中ではないこと。
         * @note 具体型はRegisterで指定した優先度の昇順に実行する。同値の場合は初回登録順。
         * @note 列挙型が異なっても数値が同じ場合は同じ処理IDとして扱う。
         * @note 関連付けた処理、削除、保留統合またはメモリ確保中に例外が送出された場合はstd::terminate()を呼び出す。
         */
        template<class Operation> 
        void Execute(Operation operation) noexcept;

        /*!
         * @brief 保留中の削除要求を処理し、保留オブジェクトを統合する。
         * @pre Execute中ではないこと。
         * @note メモリ確保またはオブジェクトの削除中に例外が送出された場合はstd::terminate()を呼び出す。
         */
        void FlushDestroyed() noexcept;

        /*!
         * @brief 全具体型の稼働数と保留数の合計を取得する。
         * @return 管理中の総オブジェクト数。
         */
        [[nodiscard]]
        std::size_t Size() const noexcept;

        /*!
         * @brief 指定した具体型の稼働数と保留数の合計を取得する。
         * @tparam T 要素数を取得する具体型。
         * @return Tの管理数。Tの格納領域が存在しない場合は0。
         */
        template<class T>
        [[nodiscard]]
        std::size_t Count() const noexcept;

    private:
        /*!
        *   @brief 型に依存しない格納領域の内部インターフェース。
        */
        class IStorage;

        /*!
        *   @brief 具体型Tのオブジェクトを固定長ブロックに格納する。
        */
        template<class T> 
        class Storage;

        /*!
        *   @brief 具体型Tの処理定義を実行プランへ登録する。
        */
        template<class T> 
        class RegistrationBuilder;

        /*!
        *   @brief 具体型の識別キーと対応する格納領域を保持する。
        */
        struct TypeEntry;

        /*!
        *   @brief 実行プラン内の1つの具体型と、その実行関数を保持する。
        */
        struct PlanNode;

        /*!
         * @brief 管理対象オブジェクトに削除要求の送信先を設定する。
         * @param[in,out] object 管理対象オブジェクト。
         * @param[in] queue オブジェクトを格納するブロックの削除要求先。
         */
        static void BindManagedObject(OptimizedObject& object, detail::IDestroyQueue* queue) noexcept;

        /*!
         * @brief Tの型要件をコンパイル時に検証する。
         * @tparam T Baseまたはその派生型で、破棄可能な具象型。
         */
        template<class T> 
        static consteval void ValidateType();

        /*!
         * @brief 具体型の格納領域を取得し、存在しなければ新たに作成する。
         * @tparam T 格納する具体型。
         * @return Tに対応する格納領域への参照。
         */
        template<class T> 
        Storage<T>& EnsureStorage() noexcept;

        /*!
         * @brief 処理IDに対応する実行プランへTの実行関数を追加または更新し、実行順を整える。
         * @tparam T 処理を実行する具体型。
         * @tparam OperationValue 処理ID。
         * @tparam Method T&で呼び出せる処理。
         * @param[in] storage Tの格納領域。
         */
        template<class T, auto OperationValue, auto Method> 
        void BindImpl(Storage<T>& storage) noexcept;

        /*!
         * @brief 具体型の格納領域にある稼働中の各オブジェクトに処理を適用する。
         * @tparam T 処理を実行する具体型。
         * @tparam Method T&で呼び出せる処理。
         * @param[in,out] storageAddress Tに対応する格納領域。
         */
        template<class T, auto Method> 
        static void ExecuteStorage(IStorage* storageAddress) noexcept;

        /*!
         * @brief 削除要求のある格納領域を次回の削除処理の対象に追加する。
         * @param[in] storage 削除要求のある格納領域。
         */
        void RegisterDestroyDirty(IStorage* storage) noexcept;

        /*!
         * @brief 保留オブジェクトのある格納領域を次回の統合処理の対象に追加する。
         * @param[in] storage 保留オブジェクトのある格納領域。
         */
        void RegisterPendingDirty(IStorage* storage) noexcept;

        /*!
        *   @brief 削除要求のある格納領域を走査し、要求されたオブジェクトを破棄する。
        */
        void FlushDestroyQueues() noexcept;

        /*!
        *   @brief 保留オブジェクトを同じ格納位置のまま実行対象に加える。
        */
        void MergePending() noexcept;

    private:
        std::vector<TypeEntry> types_;

        std::unordered_map<const void*, std::size_t> typeIndex_;

        std::unordered_map<std::uint64_t, std::vector<PlanNode>> executionPlans_;

        std::vector<IStorage*> destroyDirtyStorages_;

        std::vector<IStorage*> pendingDirtyStorages_;

        std::size_t nextRegistrationOrder_ = 0;

        bool isExecuting_ = false;
    };

} // namespace ooptimizer

#include "detail/ObjectOptimizer.inl"
