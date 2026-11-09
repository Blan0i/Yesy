## Частичное экспериментальное обновление

В этой версии для Body Widgets скрываются настройки, неприменимые к выбранному типу. Цвет подписи виджета берётся из индивидуального переопределения пина. Добавлена проверка переименования ссылок в автотесте, но тест не запускался. Цвет текста пинов в Live Preview, переключение языков и виджет кривой ещё не реализованы.

## Экспериментальное обновление: безопасность и новые виджеты

Это обновление исходников **не проверено сборкой или запуском в UE 4.20.3**. Перед установкой сохраните копии плагина и проекта/мода. Не обновляйте и не пересохраняйте ценные ассеты до испытания на копии.

* Исправлено сохранение имени в описании пина после переименования; перенаправления создаются для сохранения проводов. Уже накопившиеся неверные Pin Redirects и Pin Overrides автоматически не удаляются.
* В менеджере пинов больше не используются стандартные виджеты, удерживающие указатель на пин при перестроении. Для Bool, String, Name, Text, Int и Float доступны безопасные поля. Для остальных типов отображается «-»; изменение значений в этой панели временно недоступно.
* **Min Height** в разделе Widgets задаёт минимальную высоту ноды (0 — автоматически). Изменён способ увеличения тела ноды; проверьте его в Mod Kit, в том числе для компактных нод.
* **Scroll List** в Body Widgets — список с прокруткой для выбора одного варианта. Выберите входной пин String, Name, Text или Enum, заполните Options (у Enum варианты берутся из перечисления), задайте Label и Width. Height задаёт максимальную высоту списка (0 — 120 единиц); короткий список должен занимать только необходимую высоту, а подпись — выравниваться по его центру. Поиск и множественный выбор не реализованы. Он редактирует значение по умолчанию пина; при подключении провода используется значение по проводу. Live Preview доступен только для просмотра.
* **Slider** в Body Widgets редактирует значение по умолчанию входного Float или Int. Задайте Pin Name, Type = Slider, Slider Min и Slider Max (максимум больше минимума). Значение сохраняется при отпускании мыши; Int округляется. В Live Preview виджет доступен только для просмотра. При подключённом проводе используется значение по проводу.
* Исправлен способ показа пользовательского углового значка; проверьте его в Mod Kit.
* **Corner Icon Texture** заменяет значок в правом верхнем углу, в том числе значок latent. **Corner Icon Texture Tint** и **Corner Icon Texture Size** задают цвет и размер. Если текстура не загрузилась, используется стандартный значок.
* Для строк Body Widgets запрошен обычный курсор. Изменения высоты, курсора и значка требуют проверки в редакторе.
* **Color Wires** применяет заданный в Pin Overrides цвет пина к подключённому проводу; без индивидуального цвета провода остаются стандартного цвета типа.
* Min Height, Scroll List, Slider и настройки угловой текстуры поддерживаются импортом и экспортом JSON. Старые пресеты совместимы по формату.
* Для проверки перенаправлений и соединений используйте копии ассетов. Предупреждения CNA014 в старых ассетах остаются, пока ошибочные настройки не будут проверены и исправлены.

# NodeFactory 2.0 для Unreal Engine 4.20.3

NodeFactory позволяет описать собственную ноду Blueprint одним ассетом — **Custom Node Asset**. Логика ноды строится в макросе Execute (Execution Graph), реакции редактора — в Editor Behavior Graph (BehaviorGraph). Пины, внешний вид, флаги и локальные переменные задаются в окне ассета. Изменения сразу видны в Live Preview.

В любом Blueprint нода раскрывается при компиляции в подграф ассета. Макросы, свёрнутые графы и knot-узлы встраиваются, вложенные пользовательские ноды раскрываются рекурсивно. В сборке игры остаются только узлы движка: ассет — данные редактора и не готовится к упаковке (cook).

Интерфейс плагина написан на английском, в комплекте есть русский перевод (см. «Локализация»).

## Состав

| Путь | Содержимое |
| --- | --- |
| `NodeFactory.uplugin` | Описание, версия 2.0.4-partial-experimental, `CanContainContent`, модули, цель локализации `NodeFactory` |
| `Source/NodeFactory` | Runtime-модуль: класс ассета `UCustomNodeAsset` (наследник `UBlueprint`), типы, версия формата |
| `Source/NodeFactoryEditor` | Editor-модуль: нода `UK2Node_CustomNode`, раскрытие, валидатор, окно ассета, Pin Manager, локальные переменные, Live Preview, Behavior, JSON, каталог, сервис обновления, автотесты |
| `Resources` | `Icon128.png`, значки ассета для Content Browser |
| `Config` | `FilterPlugin.ini`, конфиги локализации `Config/Localization/*.ini` |
| `Content/Localization/NodeFactory` | Манифест, архивы, `.po` и `.locres` для `en` и `ru` |
| `Content/Examples` | Место для примеров в формате `.uasset` (см. `Content/Examples/README.md`) |
| `Presets` | Шесть примеров нод в JSON для каталога (раздел 11.3 ТЗ) |
| `BuildNodeFactory.bat` | Сборка плагина без пересборки проекта, для Mod Kit и проектов только с Blueprint (см. «Установка») |

Объём исходников: 39 `.h` и 46 `.cpp`, около 24 000 строк.

## Установка

1. Скопируйте папку `NodeFactory` в `<Проект>/Plugins/NodeFactory`.
2. Соберите плагин. Нужна Visual Studio 2015 или 2017 с C++: движок 4.20 не работает с Visual Studio 2019 и 2022. Плагин включён по умолчанию (`EnabledByDefault: true`). Если в `.uproject` есть явная запись NodeFactory с `Enabled: false`, смените её на `true`.
   * C++-проект: Generate Visual Studio project files, затем конфигурация Development Editor → Build. Если сборка закончилась за секунду с `Target is up to date`, плагин она не собрала: проверьте, что он включён в проекте.
   * Проект, который редактор пересобрать не может (Hello Neighbor Mod Kit, где нет исходников игры, или проект только с Blueprint): запустите `BuildNodeFactory.bat` из папки плагина. Скрипт собирает плагин во временном проекте командой `RunUAT BuildPlugin` и копирует готовую папку `Binaries` в папку плагина. Полный журнал сборки: `C:\NFBuild\BuildLog.txt`.
   * Если движок проекта не умеет собирать C++, скрипт берёт Unreal Engine 4.20 из Epic Games Launcher и записывает в `Binaries/Win64/UE4Editor.modules` BuildId движка проекта. Так работает только с движком, совместимым со стандартным 4.20.3; при изменённом движке модули могут не загрузиться.
3. Убедитесь, что плагин включён: Edit → Plugins → Blueprints → NodeFactory; при необходимости перезапустите редактор.

## Быстрый старт

1. Content Browser → Add New → Blueprints → **Custom Node Asset**. Новый ассет содержит Execution Graph с узлами Tunnel Input и Tunnel Output: impure-нода с пинами `execute` → `then`.
2. Откройте ассет. В окне доступны Execution Graph, Editor Behavior Graph, Node Settings (Details), Pin Manager, Local Variables, Live Preview и NodeFactory Log. Кнопки на панели инструментов: Import JSON, Export JSON, Live Preview, Validate, Recompile Node, Node Catalog.
3. Добавьте пины в Pin Manager (`+ Input`, `+ Output`) и задайте тип, контейнер, значение по умолчанию и флаги. Горячие клавиши:
   * F2 — переименовать (старое имя записывается в Pin Redirects, провода сохраняются);
   * Delete — удалить;
   * Alt+Up и Alt+Down — переместить;
   * Ctrl+F — фильтр.

   Строки также можно перетаскивать мышью.
4. Постройте логику в Execution Graph между узлами Tunnel. Затем скомпилируйте и сохраните ассет.
5. В любом Blueprint вызовите меню нод правой кнопкой. Нода находится в категории ассета, а при пустой категории — в Custom Nodes. Меню строится из тегов реестра ассетов, сами ассеты не загружаются.
6. Двойной клик по ноде открывает ассет. Контекстное меню ноды содержит Open Custom Node Asset, Refresh Node и Reset Behavior Changes.

## Обновление экземпляров

После сохранения ассета или команды Recompile Node окно подтверждения перечисляет Blueprint, в которых используется нода. Варианты: Update All, Skip или Cancel; при подключённом контроле версий можно сразу сделать checkout. Окно отключается в настройках (`bConfirmBulkUpdate`).

Пины сопоставляются по порядку: по имени, затем по Pin Redirects, затем по типу и позиции. Каждый потерянный провод даёт CNA016 в Message Log «NodeFactory», итоговый отчёт показывает число нод и список потерь. Автосохранения нет.

Если Blueprint с устаревшими нодами открыт позже, сервис обновления предлагает перестроить их при открытии. До обновления компилятор сообщает CNA006. Blueprint уровня обновляется только при открытом уровне.

## Возможности

* **Внешний вид**:
  * цвета заголовка, его текста и подписей пинов (цвет тела ноды в 4.20 задать нельзя, см. V6);
  * значок — кисть EditorStyle из каталога или текстура из Content Browser (при недоступной текстуре — CNA019);
  * угловой значок: Latent, Server, Client, Multicast или Message;
  * бейджи в углах, минимальная ширина;
  * виджеты на теле ноды: CheckBox, ComboBox, TextBox;
  * compact-заголовок (только для pure-ноды);
  * провода цвета пина;
  * бирюзовые exec-провода и заголовки событий в Behavior Graph.
* **Пины**:
  * отображаемое имя, подсказка;
  * Advanced, Hidden by default, Required (CNA008; скрытый обязательный пин не проверяется, пока Behavior его не покажет);
  * Default To Self, PassThroughOf (CNA015);
  * ConnectionRule: SameTypeOnly или ClassFilter;
  * свой цвет пина и подписи;
  * разбиение структур.
* **Динамические пины**: пин-массив становится группой с кнопкой «+», при раскрытии создаётся Make Array. Все группы ноды используют одно общее число пинов (DynamicPinCount), допустимый диапазон — пересечение их диапазонов.
* **Wildcard**: один тип на все Wildcard-пины ноды. Тип задаёт первый подключённый провод и сбрасывается после отключения всех проводов. Без провода — CNA007.
* **Behavior** (наследник `UCNABehaviorBase` в BehaviorGraph):
  * события: OnNodePlaced, OnWireChanged, OnPinValueChanged, OnAddPin, OnRemovePin, OnPinSplit, OnPinRecombined, OnAdvancedViewToggled;
  * действия: SetPinVisibility, RenamePin, ChangePinDataType, SetPinConnectionRule, SetNodeTitle, ThrowNodeError, ThrowNodeWarning;
  * запросы: GetPinValue, IsPinConnected и другие.

  Повторный вход в событие блокируется. Ошибки Behavior выводятся как CNA011.
* **Локальные переменные**: панель Local Variables, ноды Get Local и Set Local в Execution Graph.
  * У каждого экземпляра ноды свои копии: при раскрытии создаются временные переменные.
  * Переменные сбрасываются к значениям по умолчанию при каждом входе (ТЗ 4.8, п. 4).
  * В pure-ноде переменные запрещены (CNA018). Ссылка на несуществующую переменную — CNA017: компилятор проверяет ноды с проводами, Validate — все ноды.
* **Get Caller**: возвращает вызывающий объект (Self Blueprint); ContextClass задаёт требуемый класс (CNA012).
* **JSON**: Export и Import обоих графов (T3D), настроек, текстов `FText` с namespace и key, путей текстур, метаданных. Миграции выполняются по `schemaVersion`. Импорт безопасный:
  * ограничения размера файла и корней путей;
  * допускаются только классы нод схемы K2;
  * проверяется баланс Begin/End Object;
  * перед перезаписью показывается сводка;
  * импорт отменяется одним шагом Undo, при ошибке ассет откатывается.
* **Node Catalog**: Window → Tools → Node Catalog или кнопка в окне ассета.
  * Ищет по пресетам. По кнопке Import (или двойному щелчку) создаёт ассет в папке, выбранной в списке Import into; к занятому имени добавляется `_1`.
  * Кнопка Validate All проверяет все ассеты проекта.
  * Папка пресетов задаётся в настройках, по умолчанию — `<Проект>/NodeFactory/Presets`. Пока этой папки нет, показываются `Presets` плагина.
* **Validate**: команда в меню ассета и на панели инструментов, `IsDataValid` (плагин Data Validation), проверка при компиляции ассета. Коды CNA001–CNA019 описаны в разделе 10 ТЗ.
* **Development Only, Deprecated, AllowedGraphs, HiddenInMenu, подпись экземпляра** — флаги в Node Settings.

## Настройки

Editor Preferences → Plugins → NodeFactory:

| Параметр | По умолчанию | Назначение |
| --- | --- | --- |
| MaxExpansionDepth | 16 | Предел вложенности нод (CNA002) |
| bConfirmBulkUpdate | true | Окно подтверждения перед обновлением экземпляров |
| MaxImportFileSizeKB | 4096 | Предел размера импортируемого JSON |
| CatalogFolder | пусто = `<Проект>/NodeFactory/Presets` | Папка пресетов каталога |
| bVerboseLog | false | Подробный лог `LogNodeFactory` |

Состояние Live Preview (вкл./выкл.) запоминается в тех же настройках.

## Примеры

В `Presets` лежат шесть нод из раздела 11.3 ТЗ. Импортируйте их через Node Catalog в папку проекта или в `Content/Examples` плагина.

| Файл | Нода | Что показывает | Приёмка |
| --- | --- | --- | --- |
| `CNA_PureMath.json` | Multiply Add | Pure-математика A*B+C, три входа Float, compact-заголовок | A1, A11 |
| `CNA_DelayWrapper.json` | Delay Then | Обёртка над Delay: latent-значок, только Event Graph и макросы, Pin Redirect Seconds → Duration | A3, A6 |
| `CNA_BuildArray.json` | Make String List | Динамические пины Item 2..8 → Make Array, Advanced-пин Separator | A7 |
| `CNA_SwapWildcard.json` | Swap Values | Обмен двух значений через Wildcard | A8 |
| `CNA_BehaviorToggle.json` | Print With Details | Behavior показывает и скрывает пин Details, CheckBox на теле ноды | A10, A22 |
| `CNA_ApplyHealthChange.json` | Apply Health Change | Локальная переменная NewHealth (Set Local и Get Local), своё оформление: цвета, значок, бейдж LOCAL, MinWidth, цвета пинов | A11, A17, A20, A22 |

## Локализация

* Все тексты интерфейса используют LOCTEXT, NSLOCTEXT и UI_COMMAND в пространстве имён `NodeFactory`: 463 строки, английский и русский.
* Цель `NodeFactory` объявлена в `.uplugin` (LoadingPolicy Editor). Редактор загружает файлы `Content/Localization/NodeFactory/<culture>/NodeFactory.locres`.
* Как переключить язык:
  1. Editor Preferences → Region & Language → Editor Language → Русский.
  2. Если русского нет в списке редактора 4.20, запустите редактор с ключом `-culture=ru`.
* Тексты ассетов (имена, подсказки, категории, заголовки пинов) имеют тип `FText`. Их собирает Localization Dashboard проекта (Gather Text from Assets). Пресеты используют namespace `NodeFactoryPresets`.
* Как обновить перевод — запускайте из папки проекта команды `UE4Editor-Cmd.exe <Проект>.uproject -run=GatherText -config="Plugins/NodeFactory/Config/Localization/<файл>.ini"` по порядку:
  1. `NodeFactory_Gather.ini` — сбор строк;
  2. `NodeFactory_Export.ini` — выгрузка `.po`;
  3. правка `ru/NodeFactory.po`;
  4. `NodeFactory_Import.ini` — загрузка `.po`;
  5. `NodeFactory_Compile.ini` — сборка `.locres`.

## Автотесты

Session Frontend → Automation → NodeFactory. Тесты покрывают проверки A1–A13, которые не требуют интерфейса:

* `NodeFactory.Expansion`
* `NodeFactory.Update`
* `NodeFactory.Pins.Dynamic`
* `NodeFactory.Pins.Wildcard`
* `NodeFactory.Json`
* `NodeFactory.Behavior`
* `NodeFactory.Undo`
* `NodeFactory.Validation`

Запуск из командной строки:

```
UE4Editor-Cmd.exe <Проект>.uproject -ExecCmds="Automation RunTests NodeFactory; Quit" -unattended -nopause -nullrhi -log
```

Во время тестов (`GIsAutomationTesting`) диалоги плагина не показываются.

## Макросы совместимости

Если API движка отличается от ожидаемого, макрос можно отключить в `*.Build.cs` модуля: `PublicDefinitions.Add("CNA_WITH_...=0");`.

| Макрос | По умолчанию | При значении 0 |
| --- | --- | --- |
| `CNA_WITH_STYLED_PINS` | 1 | Пины используют штатные виджеты и цвета типов (запасной вариант V25) |
| `CNA_WITH_ASSET_COMPILER` | 1 | Компиляция ассета не запускает проверки; Validate по-прежнему работает |
| `CNA_WITH_DATA_VALIDATION` | 1 | `IsDataValid` не переопределяется; Validate в окне ассета работает |

## Ограничения версии 4.20

* **Полная сборка ещё не подтверждена.** Первая сборка в 4.20.3 (VS 2015) нашла несовместимости API. Они исправлены (см. «Исправления после первой сборки»). Неподтверждённые места помечены в исходниках комментарием `V-check` (список ниже).
* Фабрику компилятора (`FKismetCompilerContext::RegisterCompilerForBP`) в 4.20 нельзя снять. При Hot Reload она регистрируется один раз за сессию, остальные регистрации снимаются в `ShutdownModule`.
* Live Preview — только просмотр: read-only `SGraphEditor` на временном `UBlueprint`.
* Локальные переменные не сохраняют значение между вызовами. Для постоянного состояния используйте переменные Blueprint.
* Wildcard-пины одной ноды всегда имеют один общий тип. Compact-вид доступен только pure-ноде.
* Точка останова F9 на самой ноде (V12) и вырезание Development Only в Shipping (V5) зависят от движка и не проверены.
* Файлы `.uasset` вне редактора создать нельзя, поэтому примеры поставляются в виде JSON (`Presets`).
* Новая нода — impure с `execute` → `then`. Чтобы сделать её pure, удалите exec-пины и включите флаг Pure.

## Исправления после первой сборки

* V6 опровергнуто: `SGraphNode::GetNodeBodyTintColor` в 4.20 нет. Свойство `BodyTint` удалено из ассета, JSON-схемы и пресетов (запасной вариант ТЗ).
* V16/V24: `SGraphNode::CreateAdvancedViewArrow` в 4.20 не виртуальный. Стрелка Advanced заменяется своей после раскладки ноды.
* V25: `SGraphPinEnum` и `SGraphPinClass` в 4.20 приватные. Enum- и class-пины используют штатные виджеты движка с цветом типа. `SGraphPinNum` в 4.20 — шаблон (`SGraphPinNum<float>`).
* `IPackageAutoSaver` доступен через `GUnrealEd->GetPackageAutoSaver()`. `FText::Join` заменён на `FString::Join`.

## Точки V-check

В исходниках 30 пометок (`rg V-check Source`). Основные:

* `RegisterApplicationModes` и виртуальность `GetGraphAppearance` у `FBlueprintEditor` (`CNAAssetEditor.h`).
* `TabIcon` как `FSlateIcon` (`CNAEditorMode.cpp`).
* `FKismetCompilerContext::RegisterCompilerForBP` и виртуальный `CreateFunctionList` (`CNAValidator.h/.cpp`).
* Экспорт типизированных виджетов пинов GraphEditor (`SGraphPin_NodeFactory.h`).
* `UK2Node_MakeArray::NumInputs` (`CNAExpander.cpp`).
* `UEditorEngine::GetTimerManager`, `FAssetEditorManager::OnAssetOpenedInEditor` (`CNAUpdateService.cpp`).
* `UK2Node_EditablePinBase::CanCreateUserDefinedPin`, drag-and-drop `STableRow`, аргументы селектора типов (`SCNAPinManager.cpp`).
* `DefaultValueSimpleValidation`, `SObjectPropertyEntryBox`, `SClassPropertyEntryBox` (`SCNALocalVariables.cpp`).
* Временный `UBlueprint` без сгенерированных классов для `SGraphEditor` (`SCNALivePreview.cpp`).
* `UObject::IsDataValid` и `EDataValidationResult` (`CustomNodeAsset.h`), cook ассета `IsEditorOnly`.
* `AddExpectedError` и `EBlueprintCompileOptions::SkipGarbageCollection` в тестах.
* Создание текста с заданными namespace и key при импорте (`CNAJsonSchema.cpp`).

Вне исходников проверки требуют:

* формат T3D пресетов (copy-формат 4.20: полные пути классов в `Begin Object Class=...`, `CustomProperties Pin (...)`, `UserDefinedPin`);
* имена кистей EditorStyle в пресетах: `GraphEditor.PureFunction_16x`, `GraphEditor.Timeline_16x`, `GraphEditor.MakeArray_16x`, `GraphEditor.Select_16x`, `GraphEditor.Branch_16x`, `Kismet.AllClasses.VariableIcon`;
* формат `.locres` (Compact, версия 1, хеш источника `FCrc::StrCrc32`), а также `.manifest` и `.archive` в UTF-16;
* ключи конфигов `Config/Localization/*.ini`.
