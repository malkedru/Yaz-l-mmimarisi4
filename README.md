# LogFlow v4 — Yapılandırma Odaklı Pipeline (C++)

Artım 4: aynı çalıştırılabilir dosya, JSON yapılandırmasından farklı işlem hatları kurar. Yeni bir hat için yeniden derleme gerekmez. C++11 kaynak kodu; Windows üzerinde VS Code ve MSVC ile doğrulanmıştır. Harici C++ kütüphanesi gerekmez.

## VS Code ile derleme ve çalıştırma

1. Bu klasörü VS Code'da açın; Microsoft C/C++ eklentisini ve Visual Studio C++ Build Tools'u kurun. Bu bilgisayarda MSVC otomatik bulunur. MSVC yoksa betik PATH üzerindeki `g++` derleyicisini kullanır.
2. **Ctrl+Shift+B** ile `build/logflow.exe` derlenir. **F5** hata raporu yapılandırmasını çalıştırır (MSVC hata ayıklayıcı).
3. Terminal komutlarını proje kökünde çalıştırın:

IntelliSense derleyici yolu bu bilgisayardaki MSVC kurulumuna ayarlanmıştır.
Başka bir bilgisayara taşırken C/C++ yapılandırmasından `compilerPath` seçimini güncelleyin.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
.\build\logflow.exe --config configs/error-report.yaml
.\build\logflow.exe --config configs/api-traffic.json
.\build\logflow.exe --config configs/bandwidth-by-client.json
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Test
```

Çıktılar sırasıyla `out/errors.txt` (1 kayıt), `out/api-traffic.tsv` (3 kayıt), `out/bandwidth-by-client.tsv` (10 kayıt) olur. Üçü aynı exe ile üretilir. Bant genişliği çıkarımı her kayıt için `clientIp` ve `bytes` alanlarını verir; istemci toplamlarını hesaplayan bir aggregation aşaması değildir.

**Biçim kararı:** PDF'nin izin verdiği JSON seçildi. `error-report.yaml`, PDF'deki komutun aynen çalışabilmesi için YAML 1.2 ile uyumlu JSON sözdiziminde yazılmıştır. Yükleyici dosya uzantısından bağımsız olarak JSON okur; girintili YAML, yorumlar ve YAML etiketleri desteklenmez. Diğer örnekler `.json` uzantılıdır.

Dosya yolları **çalışma dizinine göre** çözülür. Komutları proje kökünden çalıştırın. `out/` teslimata dahildir; kendi çıktı yolunuzun üst klasörünü önceden oluşturun.

Linux / macOS / çalışan MinGW:

```sh
make
make test
./logflow --config configs/error-report.yaml
# Make olmadan:
g++ -std=c++11 -Wall -Wextra -pedantic -O2 -o logflow main.cpp
```

## Şema ve kayıt sistemi

Tam şema ve tasarım gerekçeleri `ARCHITECTURE.md` içindedir. `ConfigValue.h` JSON değerlerini gerçek türleriyle ayrıştırır; `StageFactory.h` fabrika sözleşmesini, `StageRegistry.h` isim -> fabrika eşlemesini ve tek kayıt tablosunu içerir. `PipelineLoader.h` bütün yapıyı doğruladıktan sonra mevcut `PipelineBuilder` ile çalıştırılabilir, bileşenlerine sahip bir `Pipeline` üretir.

Yeni kayıt aşaması için `Stage<LogRecord, LogRecord>` uygulayın ve `StageInstance` döndüren fabrikayı kayıt tablosuna ekleyin. Yükleyiciye `switch` veya aşama adına özel dal eklenmez. Yeni bir aşama gerçekleştirimini eklemek derleme gerektirir; mevcut aşamalardan yeni hat kurmak yalnızca yapılandırma değişikliği gerektirir.

## Hata örnekleri

Tüm yapılandırma, kaynak okunmadan ve çıktı açılmadan doğrulanır. Aşağıdaki örneklerde hata konumu ve düzeltilecek parametre gösterilir:

```text
Yapilandirma hatasi: bad.json: pipeline.stages[1].type: unknown stage 'typo'; registered: filter, parse, project
Yapilandirma hatasi: bad.json: pipeline.stages[0].params.format: required parameter missing
Yapilandirma hatasi: bad.json: pipeline.stages[1].params.value: integer required
Yapilandirma hatasi: bad.json: pipeline.stages: non-empty array required; first stage must parse clf
Yapilandirma hatasi: bad.json: JSON line 3, column 1: string expected
```

`"value": "400"` sayı değildir; `"value": 400` kullanın. Bilinmeyen parametreler, yinelenen anahtarlar, yanlış aşama sırası ve projeksiyonda silinmiş alana bağımlı aşamalar reddedilir. İlk aşama `parse`, biçim `clf` olmalıdır.

Çıkış kodları: **0** başarı, **2** kullanım/yapılandırma hatası, **1** çalışma hatası. Geçerli yapılandırmada bulunamayan girdi veya yazılamayan çıktı bir çalışma hatasıdır. Hatalı log satırları mevcut parser davranışıyla atlanır; yeni çalışma zamanı veri hata politikası Artım 5 kapsamındadır.

## Testler

`LogFlow: test` VS Code görevi veya `scripts/build.ps1 -Test` bütün test programlarını derleyip çalıştırır; başarısızlıkta durur. Mevcut parser, filtre, projeksiyon, sink, builder, CLI ve entegrasyon testleri korunmuştur. `PipelineLoaderTests.cpp` geçerli yapılandırma, bilinmeyen tür, eksik parametre, yanlış tür, boş hat; işlem öncesi doğrulama; bileşen ömrü; kayıt sistemi genişletmesi; JSON sözdizimi ve üç çıktının içeriklerini kontrol eder.

Bu bilgisayarda MSVC ile **8 test programı, 188/188 doğrulama** başarılıdır.

## Artım 3 komutları korunur

```powershell
.\build\logflow.exe data/access-small.log --min-status 400 --fields timestamp,clientIp,status,path --output out/legacy.tsv
.\build\logflow.exe --help
.\build\logflow.exe --version
```

Alanlar: `timestamp, clientIp, method, path, status, bytes, userAgent`. Seçilen alanlar verilen sırayla TAB ile ayrılır; başlık satırı yoktur. `--config`, eski girdi/filtre/çıktı seçenekleriyle karıştırılamaz. `make experiment` önceki aşama sıralaması deneyini çalıştırır.

Sürüm: `VERSION` dosyasında **v4** ve yerel Git deposunda `v4` etiketi.
Başarılı derleme, `logflow.exe` kök kopyasını da günceller. Böylece proje kökünde
`.\logflow.exe --config configs/error-report.yaml` komutu da çalışır.
