# Примеры NodeFactory (раздел 11.3 ТЗ)

Файлы `.uasset` можно создать только в редакторе, поэтому шесть примеров поставляются как JSON в папке `Presets` плагина. Чтобы получить их в виде ассетов:

1. Включите в Content Browser показ контента плагинов: View Options → Show Plugin Content.
2. Откройте Window → Tools → Node Catalog. Пока нет папки `<Проект>/NodeFactory/Presets`, каталог показывает пресеты плагина.
3. В выпадающем списке Import into выберите папку `NodeFactory Content/Examples` или любую папку проекта (`/Game/...`).
4. Для каждого пресета нажмите Import или дважды щёлкните по нему. Ассет создаётся с именем пресета; если имя занято, добавляется `_1`.
5. Сохраните ассеты (Save All).

| Ассет | Нода | Что показывает |
| --- | --- | --- |
| `CNA_PureMath` | Multiply Add | Pure-математика A*B+C, compact-заголовок |
| `CNA_DelayWrapper` | Delay Then | Обёртка над Delay: latent, только Event Graph и макросы |
| `CNA_BuildArray` | Make String List | Динамические пины, которые раскрываются в Make Array |
| `CNA_SwapWildcard` | Swap Values | Обмен значений через Wildcard |
| `CNA_BehaviorToggle` | Print With Details | Behavior показывает и скрывает пин, CheckBox на теле ноды |
| `CNA_ApplyHealthChange` | Apply Health Change | Локальная переменная и собственное оформление |

Чтобы проверить пример, поставьте ноду в Event Graph любого Blueprint (категория NodeFactory → Examples), скомпилируйте Blueprint и запустите PIE.
