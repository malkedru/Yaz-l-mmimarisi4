# LogFlow v4 - Sistem Mimarisi

Güncel yapılandırma şeması ve registry tasarımı aşağıdaki **Artım 4** bölümündedir.
Önceki Artım 3 tasarım kararları ve deneyleri temel mimarinin belgesi olarak korunmuştur.

**Mimari odak:** modülerlik, bileşilebilirlik, aşama düzeyinde tek sorumluluk.

## Mimari Diyagram

```
                       PipelineBuilder::from(...).via(...).via(...).to(...)
                                          │ kurar
                                          ▼
FileLineSource ─► ParserStage ─► PredicateFilterStage ─► ProjectionStage ─► FileSink
   (Source)      string→Record     Record→Record (1→0/1)   Record→Record (1→1)   (Sink)
   Source<string>  Stage<string,   Stage<Record,Record>    Stage<Record,Record>  Sink<Record>
                   Record>
        └──────────────── Emitter (adaptörler) ────────────────┘
```

Filtre ve projeksiyon **aynı arayüzdür** (`Stage<LogRecord, LogRecord>`): biri kaydı
eler (1→0 ya da 1→1), diğeri dönüştürür (1→1). Hat için ikisi arasında fark yoktur;
bu yüzden herhangi bir sırada, herhangi bir sayıda eklenebilirler.

## Bileşenler

| Bileşen | Sorumluluk (tek) | Durum |
|---------|------------------|-------|
| `Source<T>` / `FileLineSource` | veri üretir | değişmedi |
| `Emitter<T>` | veriyi bir sonrakine iletir | değişmedi |
| `Stage<I,O>` / `ParserStage` | tek bir dönüşüm | değişmedi |
| `Sink<T>` | veriyi tüketir | **`open()`/`close()` eklendi** |
| `PredicateFilterStage` | koşulu sağlamayan kaydı eler | YENİ |
| `ProjectionStage` | kaydı seçilen alanlara indirger | YENİ |
| `FileSink` | kayıtları dosyaya yazar | YENİ |
| `PipelineBuilder` / `PipelineStep<T>` | aşamaları tip güvenli birleştirir | YENİ |
| `Pipeline` | kurulu hattı çalıştırır, yaşam döngüsünü yönetir | **yeniden yazıldı** |
| `Predicates` | hazır koşullar (`statusAtLeast`, `pathStartsWith`, `timestampBetween`) | YENİ |
| `LogFields` | alan adları ve metin gösterimi (tek doğruluk kaynağı) | YENİ |
| `CommandLine` | argümanları `Options`'a çevirir | YENİ |

## Tasarım Kararları

### 1. PipelineBuilder: tip güvenli, sıra ve sayı serbest

```cpp
Pipeline p = PipelineBuilder::from(source)     // PipelineStep<string>
                 .via(parser)                  // PipelineStep<LogRecord>
                 .via(filter)                  // PipelineStep<LogRecord>
                 .via(projection)              // PipelineStep<LogRecord>
                 .to(sink);                    // Pipeline
p.run();
```

* `via()` yalnızca girdi tipi **zincirin o anki çıktı tipine** uyan bir `Stage` kabul eder.
  `string` akarken bir filtre eklemeye çalışmak, program çalışmadan **derleme hatası** verir.
* `from()` / `via()` zinciri küçük bir işlev bileşimi olarak kurar: her `via()`, önceki
  zinciri saran yeni bir işlev üretir. Bellekte ayrıca bağlı liste, `void*` ya da
  çalışma zamanı tip denetimi yoktur.
* Builder aşamaları **sahiplenmez**; nesneler `run()` bitene dek çağıran tarafta yaşar.
  Seçeneğe bağlı aşamalar için `main.cpp`, aşamaları `unique_ptr` listesinde tutar ve
  döngüyle `via()` çağırır (`filtre` ve `projeksiyon` aynı tip olduğundan bu mümkündür).
* **Neden eski `Pipeline<I,O>` kaldırıldı?** Tek aşamaya (`Source→Stage→Sink`) sabitlenmişti.
  Yeni `Pipeline` tipten bağımsızdır; tipler builder'ın içinde derleme zamanında denetlenir.
* 4. haftada yapılandırma dosyası bu builder'ı kullanacak: aşama listesini dosyadan okuyup
  aynı `via()` çağrılarını yapacak; builder'ın kendisi değişmeyecek.

### 2. Yaşam döngüsü (lifecycle) artık gerçek

| Olay | Sıra | Gerekçe |
|------|------|---------|
| `open()` | sink → ... → kaynağa en yakın aşama | hedef hazır olmadan veri akmasın |
| `close()` | kaynağa en yakın aşama → ... → sink | aşamalardaki arabellek, hedef kapanmadan boşalsın |

* `open()` yarıda hata verirse, o ana kadar açılanlar kapatılır.
* Akış sırasında hata olursa `close()` **yine çağrılır**; asıl hata korunur.
* Normal bitişte `close()` hatası (disk dolu vb.) **bildirilir**, yutulmaz.
* `FileSink::close()` arabelleği boşaltır, akışı kapatır ve hatayı denetler. `close()`
  hiç çağrılmayan bir dosya sink'i, arabellekteki son satırları sessizce kaybedebilir
  ve yazma hatalarını fark etmez; hattın her koşulda `close()` çağırması bu yüzden
  mimarinin parçasıdır, süs değildir. Testler: `FileSinkTests`, `PipelineBuilderTests`.

### 3. Projeksiyon ve veri minimizasyonu

`ProjectionStage` seçilmeyen alanları boşaltır (metin `""`, sayı `0`) ve **ham satırı
(`raw`) ile `attributes`'u her zaman atar**: ham satır, seçilmeyen alanların (IP,
user agent, sorgu dizesindeki veriler) tamamını içerir; atılmasaydı projeksiyon
yalnızca görünüşte bir gizlilik önlemi olurdu.

Çıktı hâlâ `LogRecord` olduğundan projeksiyon hattın her yerine konabilir. Bedeli:
**projeksiyondan sonra gelen filtre, atılmış alanı yalnızca boş/0 değeriyle görür**
(Deney C).

### 4. Çıktı biçimi

`ConsoleSink` ve `FileSink` bir `RecordFormatter` alır. `--fields` verilirse seçilen
alanlar, verilen sırayla, TAB ile ayrılarak yazılır (başlık satırı yok); verilmezse
v2'deki okunabilir tek satırlı biçim kullanılır.

## Aşama Eklemek Neleri Değiştirdi?

* **Yeni:** `PredicateFilterStage.h`, `ProjectionStage.h`, `FileSink.h`, `Predicates.h`,
  `LogFields.h`, `PipelineBuilder.h`, `CommandLine.h`.
* **Değişen:** `Sink.h` (yalnızca varsayılan `open()/close()` eklendi), `Pipeline.h`,
  `ConsoleSink.h` (biçimleyici alır), `main.cpp`.
* **Değişmeyen:** `Emitter.h`, `Source.h`, `Stage.h`, `FileLineSource.h`, `LogRecord.h`,
  `ParserStage.h`. Üç yeni aşama, mevcut arayüzlere **dokunmadan** eklendi.

## Sıralama Deneyi (Görev 4)

`experiments/OrderExperiment.cpp` (`make experiment`): 500 000 satırlık, sabit tohumlu
sahte log; ≈%10 kayıt `status >= 400`, ≈%30 kayıt `/api`. Aynı aşama türleri, yalnızca
`via()` sırası değişir. Sayaçlar **kesindir** (her çalıştırmada aynı); süreler en iyi
5 çalıştırmadır ve makineye göre değişir (aynı yapılandırma üç çalıştırmada
≈220–250 ms arasında oynadı).

**Başlangıç noktası: ayrıştırmadan önce filtrelemek mümkün değildir.** Koşullar
`LogRecord` alanlarına bakar; `ParserStage` her zaman ilk filtreden önce gelmek zorundadır.
Bu, sıranın bir *bağımlılık* tarafından belirlendiği durumdur. Aşağıdakiler ise gerçek
tercihlerdir. Ayrıştırma tek başına toplam sürenin kabaca üçte ikisini oluşturur
(ayrı ölçüm: 500 000 satır ≈175 ms), bu yüzden aşağıdaki süre farkları o tabanın üstünde kalır.

### Deney A - iki filtrenin sırası (`status >= 400` ve `path` önek `/api`)

| Sıra | durum sınama | yol sınama | çıktı | süre |
|------|-------------:|-----------:|------:|-----:|
| durum → yol | 500 000 | 49 656 | 14 941 | ≈220–250 ms |
| yol → durum | 150 668 | 500 000 | 14 941 | ≈240–250 ms |

* **Çıktı özdeş** (satır satır karşılaştırıldı).
* Daha eleyici filtre (`status`, ≈%10 geçer) önce gelince toplam koşul sınaması
  549 656'ya düşer; ters sırada 650 668 (**≈%18 fazla**).
* Süre farkı gürültünün içinde kaldı: iki koşul da çok ucuz (bir tamsayı karşılaştırması
  ve kısa bir önek karşılaştırması). Kural: **ucuz ve daha eleyici olanı öne al**; koşullar
  pahalı olsaydı (düzenli ifade, arama) fark süreye de yansırdı.

### Deney B - projeksiyon filtreden önce mi, sonra mı?

| Sıra | projekte edilen kayıt | çıktı | süre |
|------|----------------------:|------:|-----:|
| durum → yol → projeksiyon | 14 941 | 14 941 | ≈220–250 ms |
| projeksiyon → durum → yol | 500 000 | 14 941 | ≈290–330 ms |

* **Çıktı özdeş**; ama projeksiyon her kayıt için yeni bir `LogRecord` (dizgi kopyaları,
  bellek ayırma) üretir. Filtre önce gelince **33 kat daha az** kayıt projekte edilir ve
  üç çalıştırmanın üçünde de daha hızlıdır (projeksiyon-önce ≈%15–45 daha yavaş).
* Filtre önce de gizlilik açısından zararsızdır: elenen kayıtlar hiç kopyalanmaz.

### Deney C - projeksiyon, filtrenin alanını atarsa

`--fields timestamp,clientIp,path` ( `status` yok):

| Sıra | çıktı |
|------|------:|
| filtre → projeksiyon | 14 941 satır |
| projeksiyon → filtre | **0 satır** |

Projeksiyon önce çalışınca `status` 0 olur ve `status >= 400` hiçbir kayda uymaz.
**Sıra burada sonucu değiştirir**, hata vermeden. Bu yüzden komut satırı programı filtreleri
her zaman projeksiyondan **önce** kurar; sıra, tasarımın bir parçası olarak sabittir.

### Sonuç

* Filtre sırası: sonucu değiştirmez, **iş miktarını** değiştirir (bağımsız filtreler).
* Filtre/projeksiyon sırası: kural olarak **önce filtre** (hem daha az iş hem güvenli);
  projeksiyonun önce gelmesi yalnızca filtrelerin kullandığı tüm alanlar korunuyorsa
  *aynı sonucu* verir.
* Ayrıştırma, hep ilk aşamadır (bağımlılık).

## Artım 4 — Yapılandırma Şeması ve Registry

Artım 4'te topoloji `main.cpp` dışına taşınmıştır. Akış:

```text
JSON -> ConfigParser -> ConfigValue (türlü ağaç)
     -> PipelineLoader -> StageRegistry[name] -> StageFactory(params)
     -> sahip olunan source / parser / record stages / sink
     -> mevcut PipelineBuilder -> Pipeline -> run()
```

### JSON şeması

Kök yalnızca `pipeline` nesnesini içerir. Aşağıdaki alanların hepsi zorunludur; bilinmeyen anahtarlar yazım hatalarını gizlememek için reddedilir.

| Yol | Tür / kural |
| --- | --- |
| `pipeline.name` | Boş olmayan metin; hattın açıklayıcı adı |
| `pipeline.source` | `{ "type": "file", "params": { "path": "..." } }` |
| `pipeline.stages` | Boş olmayan aşama dizisi, sırası aynen korunur |
| `pipeline.stages[i]` | `{ "type": "kayıtlı-ad", "params": { ... } }` |
| `pipeline.sink` | Dosya: `{ "type": "file", "params": { "path": "..." } }`; konsol: `{ "type": "console", "params": {} }` |

Aşama fabrikaları kendi parametre şemalarına sahiptir:

| Kayıtlı tür | Parametreler | Girdi -> çıktı |
| --- | --- | --- |
| `parse` | `format`: metin, yalnızca `clf` | string -> LogRecord; yalnızca ilk sırada |
| `filter` | `field`: bilinen alan, `op`: karşılaştırma, `value`: alanla uyumlu tür | LogRecord -> LogRecord (0 veya 1) |
| `project` | `fields`: boş olmayan, tekrarsız, bilinen alan adları dizisi | LogRecord -> LogRecord |

`filter.op`: `==`, `!=`, `>=`, `<=`, `>`, `<`; metin alanları için ayrıca `startsWith`. `status` ve `bytes` değerleri JSON tamsayısıdır (tırnaklı sayı, ondalık sayı, bool ve null kabul edilmez). Status 100..599, bytes >= 0 olmalıdır. Diğer alanlarda boş olmayan metin gerekir; karşılaştırma büyük/küçük harfe duyarlıdır. `timestamp` karşılaştırması UTC ISO metniyle yapılır; sayısal epoch kabul edilmez. Sıralı tarih karşılaştırmalarında `YYYY-MM-DDTHH:MM:SSZ` biçimini kullanın.

Dosya yolları çalışma dizinine göredir. Girdi ve çıktı aynı normalize dosya yolu olamaz. Çıktı üst klasörü mevcut olmalıdır. Projeksiyon sonrası TSV biçimi son projeksiyonun alan sırasını kullanır; projeksiyon yoksa mevcut okunabilir ConsoleSink biçimi kullanılır.

### Yaşam süresi ve doğrulama

`PipelineLoader.load()` dosyayı okur, `loadText()` doğrular ve `Pipeline` döndürür. Doğrulama sırasında kaynak üretilmez, sink açılmaz. Aşama fabrikaları yalnızca nesne kurmalıdır; I/O veya yan etki yapmamalıdır. Yükleyici geçerli topolojiyi mevcut builder ile birleştirir; `shared_ptr<LoadedComponents>` dönen hattın fonksiyonunda saklanır. Loader ve geçici değerler yok olsa da bileşenler çalıştırma tamamlanana kadar yaşar. Mevcut Pipeline'ın hata durumunda kapatma ve hata koruma davranışı kullanılır.

`StageInstance` parser veya kayıt aşaması taşır. `requiredFields` aşamanın ihtiyaç duyduğu alanları, `fields` projeksiyondan sonra korunan alanları bildirir. Yükleyici fabrika metadatasından tür sırasını ve silinmiş alan bağımlılıklarını denetler; aşama türü adına göre dallanmaz. Tekrar projeksiyon önce silinen alanı geri getiremez.

`StageRegistry` bir `std::map<std::string, StageFactory>` kullanır. Tek yerleşik kayıt tablosu `BuiltinStages::registry()` içindedir. Yinelenen, boş isimli veya boş fonksiyonlu kayıt reddedilir. Özel bir registry `PipelineLoader(registry)` ile verilebilir. Yeni kayıt fabrikası eklemek loader değişikliği gerektirmez; testte `select` adıyla alternatif fabrika kaydı kullanılır. Bu C++ çözümü tek kayıt tablosu yaklaşımıdır; DLL yükleme veya reflection gerektirmez.

### Hata ayrımı ve teslim örnekleri

`ConfigError`: sözdizimi, eksik/yanlış tür, bilinmeyen isim, topoloji veya parametre hatası; dosya ve alan yolu gösterilir, CLI çıkış kodu 2. JSON sözdizimi hatalarında satır/sütun bildirilir; yinelenen nesne anahtarları reddedilir; Unicode kaçışları desteklenir, iç içe veri derinliği 64 ile sınırlandırılır.

`StageException`: geçerli hattın çalıştırılması sırasında dosya erişimi/yazma/okuma hatası; CLI çıkış kodu 1. Hatalı log satırlarını atlayan mevcut parser politikası korunur. Artım 5'in veri hatası politikası bu sürümün kapsamı değildir.

- `configs/error-report.yaml`: status >= 400; timestamp, clientIp, status, path -> out/errors.txt.
- `configs/api-traffic.json`: path startsWith /api; timestamp, method, path, status -> out/api-traffic.tsv.
- `configs/bandwidth-by-client.json`: clientIp, bytes -> out/bandwidth-by-client.tsv. Bu bir çıkarımdır; istemci başına toplama içermez.

JSON seçimi PDF'nin YAML veya JSON gereksinimini karşılar. PDF'deki tam komut için `.yaml` adlı ilk dosya JSON (YAML 1.2 altkümesi) içerir. Yalnız JSON sözdizimi desteklenir; klasik girintili YAML desteklenmez.
