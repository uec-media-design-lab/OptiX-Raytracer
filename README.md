# OptiX-Raytracer

OptiX 7 をベースとしたレイトレーサーです。OptiXのAPIを意識することなく、基本的にはシーンの記述（座標変換やジオメトリ、マテリアル等）のみでレンダリングが可能です。
さらに、OptiX 7 の煩雑な処理に対するラッパーライブラリと、ユーザー定義によるジオメトリやマテリアル、テクスチャの簡易な登録システムを提供します。

![output.png](result/output.png)