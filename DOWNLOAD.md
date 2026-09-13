# Peakeater Spectral ダウンロードと導入

## 製品概要

Peakeater Spectralは、PeakEaterを基礎に改良したGPL-3.0のオープンソース・
スペクトルリミッター／マキシマイザーです。音圧、True Peak安全性、トランジェント、
低域のモノ互換性を重視しています。

## ダウンロード

[GitHub Releases](https://github.com/brunouesugi-rgb/Peakeater-Spectral/releases)

最新のWindows ZIPをダウンロードして展開してください。

## 簡易インストール

展開したフォルダで、次を実行します。

```text
tools\\install-peakeater-spectral.bat
```

VST3とCLAPがユーザー用フォルダへコピーされます。管理者権限が必要なシステム共有先へ
インストールする場合は、PowerShellを管理者として起動し、次を実行してください。

```powershell
.\\tools\\install-peakeater-spectral.ps1 -SystemInstall
```

## Ableton Live

インストール後にAbleton Liveを再起動してください。表示されない場合は、Preferencesの
Plug-ins設定から再スキャンを実行します。VST3とCLAPの検索先は、Ableton Live側の設定に
合わせてください。

## 手動インストール先

Windows VST3:

```text
C:\Program Files\Common Files\VST3
```

Windows CLAP:

```text
C:\Users\<ユーザー名>\.clap
```

## ライセンス

ソースコードはGPL-3.0です。PeakEater由来の改変版であることを明記し、再配布時は対応する
ソースコードとライセンス表示を提供してください。
