#pragma once

#include <cassert>

namespace ooptimizer::detail {

    /*!
     * @brief 削除要求と状態確認を格納領域へ委譲する内部インターフェース。
     */
    class IDestroyQueue {
    public:
        /*!
        *   @brief 削除要求のインターフェースを通して派生型を破棄する。
        */
        virtual ~IDestroyQueue() = default;

        /*!
         * @brief 指定オブジェクトの削除要求を登録する。
         * @param[in] object 削除を要求する管理対象オブジェクト。
         */
        virtual void QueueDestroy(OptimizedObject* object) noexcept = 0;

        /*!
         * @brief 指定オブジェクトが削除要求済みか確認する。
         * @param[in] object 状態を確認する管理対象オブジェクト。
         * @return 削除要求済みの場合はtrue、それ以外はfalse。
         */
        [[nodiscard]]
        virtual bool IsDestroyRequested(const OptimizedObject* object) const noexcept = 0;
    };

} // namespace ooptimizer::detail

namespace ooptimizer {

    inline void OptimizedObject::Destroy() noexcept {
        assert(destroyQueue_ != nullptr && "このオブジェクトはObjectOptimizerに管理されていません。");

        destroyQueue_->QueueDestroy(this);
    }

    inline bool OptimizedObject::IsDestroyRequested() const noexcept {
        return destroyQueue_ != nullptr && destroyQueue_->IsDestroyRequested(this);
    }

} // namespace ooptimizer
