# Artım 4 gereksinim karşılıkları

| PDF gereksinimi | Gerçekleştirim / doğrulama |
| --- | --- |
| Bildirimsel YAML veya JSON topolojisi | JSON; `configs/`, README biçim açıklaması ve ARCHITECTURE şeması |
| StageFactory ve StageRegistry | `StageFactory.h`, `StageRegistry.h`; tek kayıt tablosu ve isim/fonksiyon map'i |
| PipelineLoader | `PipelineLoader.h`; mevcut PipelineBuilder ile Pipeline üretir |
| Tür denetimi, zorunlu parametreler | ConfigValue ve fabrika doğrulaması; bilinmeyen anahtarlar da reddedilir |
| Veri işlenmeden önce açık hata | Loader yalnızca nesne kurar; I/O ancak run sırasında başlar; sentinel çıktı testi |
| Yapılandırma / çalışma zamanı hata ayrımı | ConfigError (kod 2), StageException (kod 1), README örnekleri |
| En az üç farklı yapılandırma | error-report, api-traffic, bandwidth-by-client; aynı exe ile 1 / 3 / 10 kayıt |
| Switch yerine veri odaklı kayıt | BuiltinStages kayıt tablosu; özel registry ile loader değiştirmeden genişletme testi |
| Geçerli yapılandırma testi | PipelineLoaderTests: validAndLifetime |
| Bilinmeyen aşama testi | PipelineLoaderTests: requiredCases |
| Eksik parametre testi | PipelineLoaderTests: requiredCases |
| Yanlış tür testi | PipelineLoaderTests: requiredCases |
| Boş hat testi | PipelineLoaderTests: requiredCases |
| İstenen --config komutu | CommandLine, main; configs/error-report.yaml JSON/YAML uyumlu içerik |
| v4 sürümü | VERSION, --version, Git v4 etiketi |
| VS Code C++ geliştirme | .vscode/tasks.json, launch.json, scripts/build.ps1 |

Yerleşik aşamalarla yeni hat oluşturmak derleme gerektirmez. Yeni bir C++ aşama
gerçekleştirimini eklemek ve kayıt tablosuna bağlamak derleme gerektirir.
