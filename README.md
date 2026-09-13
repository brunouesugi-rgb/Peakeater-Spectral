# Peakeater Spectral

Peakeater Spectralは、AXLRTR Audio LabによるGPL-3.0ライセンスのスペクトル・
リミッター／マキシマイザーです。オープンソースのPeakEaterを土台にした独立した
コミュニティ改良版で、スペクトル処理、Type別の音圧制御、トランジェント保護、
低域保護、True Peak安全性、CPU効率の改善を含みます。

本プロジェクトはPeakEaterの作者とは独立しており、明示された場合を除いて公式な
関係や承認を意味しません。PeakEater由来の表示とライセンス条件は維持してください。

## 特徴

- プロジェクト互換性のため、元の `Threshold` パラメータIDを維持したDrive中心の
  リミッター・ワークフロー
- EDM、Hip Hop、Drums、One Shot、One Shot Clean、Acoustic、Vocal、Bass、Bright、
  Glue、Clean、Percs、Dubstep、DrumNBass、House、Trap、808&Kick向けのType別処理
- 12バンド・リミッターと32バンド・スペクトル制御。Quality設定に応じて内部処理を
  調整し、リアルタイムCPU負荷を管理
- Ceiling、Detector HP、Saturation、Tone、Tone Mode、Attack、Hold、Release、
  Transient Recovery、Lookahead、Output、Dry/Wet
- Input Peak、Output Peak、True Peak推定値、LUFS-S推定値、RMS、Crest Factor、
  Gain Reduction、Clip Amountを確認できるメーターパネル
- 処理後の波形を確認できるリアルタイム・オシロスコープ
- Windows VST3およびCLAPビルド。選択したCMake設定では他形式も生成できます

## 状態

Peakeater Spectralはオープンソースの開発リリースです。Releaseビルド、プロジェクトの
回帰テスト、Windows VST3のpluginval strictness 10検証を実施しています。実際の制作で
使用する前に、対象のDAWと素材で動作を確認してください。

## インストール

### Windows簡易インストーラー

パッケージ内の `tools\\install-peakeater-spectral.bat` を実行すると、管理者権限なしで
VST3とCLAPをユーザー用フォルダへコピーできます。インストール後、DAWを再起動し、必要に
応じてプラグインを再スキャンしてください。

### Windows VST3

```text
C:\Program Files\Common Files\VST3
```

### Windows CLAP

```text
C:\Users\<ユーザー名>\.clap
```

### macOS

```text
/Library/Audio/Plug-Ins/VST3
/Library/Audio/Plug-Ins/Components
/Library/Audio/Plug-Ins/CLAP
/Library/Audio/Plug-Ins/LV2
```

### Linux

```text
~/.vst3
~/.clap
~/.lv2
```

## ビルド

このプロジェクトはCMake、Conan、JUCE、PeakEater由来のビルド構成を使用します。

```powershell
.\.venv\Scripts\conan.exe build . -pr:h config/conan/windows-local.jinja -pr:b config/conan/windows-local.jinja
```

VST3の検証:

```powershell
pluginval.exe --strictness-level 10 --verbose --validate-in-process "Peakeater Spectral.vst3"
```

CLAPのビルド:

```powershell
cmake --build build\Fast --config Release --target peakeater_spectral_3_CLAP
```

## DSP構成

```text
Drive -> 12バンド・リミッター -> 32バンド・スペクトルバンク -> Final Ceiling
```

Driveで音圧を上げ、Ceilingで出力を保護し、素材に合うTypeを選び、Toneを調整して、
メーターで結果を確認するワークフローを目指しています。

## 上流プロジェクト、改変、ライセンス

Peakeater SpectralはGPL-3.0ライセンスで配布します。詳細は `LICENSE.md` を参照してください。
本プロジェクトはオープンソースのPeakEaterから派生しています。再配布時は上流の表示と
GPL-3.0の条件を維持し、改変版であることを明記してください。PeakEaterの作者がこの改変版を
支持していると誤解させる表現は禁止します。

## ダウンロード

[GitHub Releases](https://github.com/brunouesugi-rgb/Peakeater-Spectral/releases)から
Windows版を入手できます。ソースコード、ライセンス、ビルド手順、簡易インストーラーは
このリポジトリで確認できます。
