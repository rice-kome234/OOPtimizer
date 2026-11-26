# OOPtimizer

異なる型のオブジェクトを、具体型ごとの固定長ブロックにまとめて格納し、指定した処理を型ごとに一括実行する C++20 ライブラリです。ゲームの敵や弾など、共通のインターフェースを持つ多数のオブジェクトを更新する場面を想定しています。

## 導入

1. [`ooptimizer/`](ooptimizer/) フォルダを、`detail/` 内の `.inl` ファイルごとプロジェクトに追加します。
2. C++20 を有効にして、`#include "ooptimizer/ObjectOptimizer.h"` を追加します。別の場所に置く場合は、`ooptimizer/` の親フォルダをインクルードパスに指定します。

ヘッダーオンリーなので、ライブラリのビルドやリンクは不要です。利用側のプログラムから呼び出せます。

## 最小サンプル

共通の基底型を `ooptimizer::OptimizedObject` から派生させ、公開型 `OOptimizerOperations` で処理IDと呼び出す処理を対応付けます。以下は、弾を敵より先に更新する例です。

```cpp
#include "ooptimizer/ObjectOptimizer.h"

#include <iostream>

enum class GameOperation {
    Update
};

class GameObject : public ooptimizer::OptimizedObject {
public:
    virtual ~GameObject() = default;
    virtual void Update() noexcept = 0;

    using OOptimizerOperations =
        ooptimizer::OperationSet<
            ooptimizer::OperationBinding<GameOperation::Update,[]<class T>(T& object) noexcept {object.T::Update();}>
        >;
};

class Enemy final : public GameObject {
public:
    void Update() noexcept override{
        std::cout << "Update Enemy" << "\n";
    }
};

class Bullet final : public GameObject {
public:
    void Update() noexcept override{
        std::cout << "Update Bullet" << "\n";
    }
};

int main(){
    ooptimizer::ObjectOptimizer<GameObject> objects;

    objects.Register<Enemy>(0, 20);
    objects.Register<Bullet>(0, 10);
    objects.Create<Enemy>();
    objects.Create<Bullet>();

    objects.Execute(GameOperation::Update);
}
```

## 実行順と処理の指定

- `Register<T>(reserveCount, priority)` の第1引数は事前確保する要素数です。`0` なら事前確保しません。第2引数は型ごとの実行優先度で、数値の小さい順に実行します。同じ優先度の場合は、初めて登録した順です。
- `Create<T>(args...)` は引数を `T` のコンストラクタへ渡し、生成した要素への参照を返します。処理の対象にする型はあらかじめ `Register<T>()` で登録します。
- `Execute(operation)` の引数は実行する処理IDです。サンプルでは `Update` を選び、登録した型を優先度順にまとめて更新します。

サンプルの `object.T::Update()` は具体型 `T` を指定して呼び出すため、この呼び出しでは仮想ディスパッチを使いません。処理の対応付けに仮想メンバー関数のポインタをそのまま渡した場合は、通常の仮想呼び出しになります。

## 参照と削除

生成した要素は生存中に移動しません。容量を増やしたり、別の要素を削除したりしても、その要素への参照やポインタは保持できます。

要素自身の `Destroy()` で削除を予約します。次の例では、最小サンプルと同じ `objects` に敵を1体追加し、その敵を削除します。

```cpp
auto& enemy = objects.Create<Enemy>();
enemy.Destroy();           // 削除を予約
objects.FlushDestroyed();  // ここで実際に削除
// ここから先は enemy を使わない
```

`FlushDestroyed()` を明示的に呼ばなくても、削除予約は `Execute()` の前後に反映されます。たとえば `Update()` の中で `Destroy()` を呼ぶと、その場では破棄せず、実行終了時に削除されます。対象が実際に削除された後や、管理する `ObjectOptimizer` が破棄された後は、その参照やポインタを使えません。

実行中に生成する型は事前登録が必要です。生成した要素は実行終了時に追加され、次の `Execute()` から対応する処理の対象になります。同じ `ObjectOptimizer` インスタンスへの並行操作には対応していません。

## ベンチマーク

敵（`Enemy`）と弾（`Bullet`）を各4,000個、計8,000個用意し、全件を20,000回更新しました。各方式で合計1億6,000万回の `Update()` を実行しています。

| 実装 | 実行時間の中央値 | 仮想関数ループに対する速度比 |
| --- | ---: | ---: |
| 個別確保した要素を `std::vector<std::unique_ptr<GameObject>>` で管理し、仮想関数で更新 | 168.300 ms | 1.00倍 |
| OOPtimizerで型ごとの固定長ブロックに連続配置し、型別にまとめて更新 | 62.755 ms | 2.68倍 |

- 測定環境：Intel Core i7-13650HX、MSVC 19.52、Release x64（`/O2`、`/GL`、LTCG）。
- 測定方法：ローカルのベンチマークを5回実行し、各方式の中央値を比較。型の登録、要素の生成、チェックサムの計算は計測外です。
- 結果確認：更新後のチェックサムは、両方式で5回とも一致しました。

この差には、データ配置と関数の呼び出し方の違いが両方含まれます。仮想呼び出しの有無だけを測った値ではなく、処理内容や実行環境によって結果は変わります。

## ライセンス

[MIT License](LICENSE)
