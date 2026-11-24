#pragma once

namespace ooptimizer {

    namespace detail {

        class IDestroyQueue;

    } // namespace detail

    /*!
     * @brief ObjectOptimizerで管理されるオブジェクト用の基底クラス。
     * @details Destroy()は実削除を行わず、所属する格納領域へ削除要求を登録します。
     */
    class OptimizedObject {
    public:
        /*!
         * @brief このオブジェクトの削除を要求する。
         *
         * @details 同じオブジェクトから複数回呼び出しても、削除要求は1回だけ登録されます。
         *          実際の削除はObjectOptimizerが削除待ちキューを処理するときに行わます。
         *
         * @pre ObjectOptimizerに管理されていること。
         * @note 削除要求の登録中に例外が送出された場合はstd::terminate()を呼び出す。
         */
        void Destroy() noexcept;

        /*!
         * @brief このオブジェクトが削除要求済みか確認する。
         * @return 削除要求済みの場合はtrue、それ以外はfalse。
         */
        [[nodiscard]]
        bool IsDestroyRequested() const noexcept;

    protected:
        /*!
         * @brief 派生型からのみ使用できる標準の特殊メンバー関数。
         *
         * @details 管理先は各オブジェクトの格納位置に紐づく。コピー・ムーブ構築では引き継がず、
         *          代入では代入先の管理先を維持する。
         */
        OptimizedObject() = default;

        OptimizedObject(const OptimizedObject&) noexcept {}

        OptimizedObject& operator=(const OptimizedObject&) noexcept { return *this; }

        OptimizedObject(OptimizedObject&&) noexcept {}

        OptimizedObject& operator=(OptimizedObject&&) noexcept { return *this; }

        ~OptimizedObject() = default;

    private:
        template<class> 
        friend class ObjectOptimizer;

        void BindDestroyQueue(detail::IDestroyQueue* queue) noexcept {
            destroyQueue_ = queue;
        }

    private:
        detail::IDestroyQueue* destroyQueue_ = nullptr;
    };

} // namespace ooptimizer

#include "detail/OptimizedObject.inl"
