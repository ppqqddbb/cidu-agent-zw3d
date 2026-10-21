    // 设备选型结果 ↔ 参数化模型 manifest 参数的一对一显式映射。
    // 配置源：config/equipment_model_parameter_mapping.json
    const EQUIPMENT_MODEL_PARAMETER_MAPPING = {
      version: 1,
      legacy_name_aliases: {
        '干渣井': '干渣井型号',
        '干渣仓': '渣仓型号',
        '干渣仓-直径（mm）': '渣仓直径',
        '干渣仓-顶标高（mm）': '渣仓顶标高',
        '干渣仓-底锥锥体高度（mm）': '渣仓锥段高度',
        '干渣仓-柱段高度（mm）': '渣仓柱段高度',
        '液压破碎关断门': '液压破碎关断门型号',
        '干渣机': '干渣机规格',
        '鳞斗干渣机': '鳞斗干渣机型号',
        '锅炉下联箱标高': '下联箱标高 [mm]',
        '锅炉下联箱标高 T1': '下联箱标高 [mm]',
        '下联箱梳形板长度': '锅炉梳型密封板长度 [mm]',
        '下联箱梳形板宽度': '锅炉梳型密封板宽度 [mm]',
        '关断门口间距': '渣井口间距 [mm]',
        '关断门箱体长度': '锅炉梳型密封板长度 [mm]',
        '关断门箱体宽度': '锅炉梳型密封板宽度 [mm]',
        '关断门箱体高度': 'L密封板自身高度 [mm]',
        '渣井下口标高': '渣井壁板下口高度 [mm]',
        '渣井壁板上口标高': '渣井壁板上口高度 [mm]',
        '渣井左右壁板角度': '渣井左右端壁板角度 [度]',
        '渣井壁板角度': '渣井左右端壁板角度 [度]'
      },
      models: {
        '渣仓': {
          keywords: ['渣仓', '干渣仓', 'silo'],
          equipment_prefixes: ['渣仓'],
          mappings: [
            { equipment: '渣仓型号', model: ['规格型号', '型号', '物料编码前缀', '设备型号'] },
            { equipment: '渣仓直径', model: ['渣仓直径', '混合直径', '直径', 'D1', '仓体直径', '筒体直径'] },
            { equipment: '渣仓顶标高', model: ['锥体顶标高', '顶标高', 'CT1', '渣仓顶标高'] },
            { equipment: '渣仓锥段高度', model: ['锥段高度', '锥体高度', '底锥高度', '底锥锥体高度'] },
            { equipment: '渣仓柱段高度', model: ['柱段高度'] },
            { equipment: '渣仓卸料类型', model: ['卸料类型', '卸料口类型', '卸料口数', '卸料口'] },
            { equipment: '渣仓有效容积', model: ['有效容积', '贮渣容积', 'CV1'] }
          ]
        },
        '关断门': {
          keywords: ['关断门', '液压破碎', '破碎关断'],
          equipment_prefixes: ['关断门', '液压破碎关断门'],
          mappings: [
            { equipment: '液压破碎关断门型号', model: ['规格型号', '型号', '设备型号'] },
            { equipment: '渣井口间距 [mm]', model: ['门口间距', '口间距', '门间距', '渣井口间距'] },
            { equipment: '关断门定位距离', model: ['定位距离', '定位间距'] },
            { equipment: '锅炉梳型密封板长度 [mm]', model: ['箱体长度', '壳体长度', '长度', '锅炉梳型密封板长度'] },
            { equipment: 'L密封板自身高度 [mm]', model: ['箱体高度', '壳体高度', '高度', 'L密封板自身高度'] },
            { equipment: '锅炉梳型密封板宽度 [mm]', model: ['箱体宽度', '壳体宽度', '宽度', '锅炉梳型密封板宽度'] }
          ]
        },
        '膨胀节': {
          keywords: ['膨胀节', '补偿器'],
          equipment_prefixes: ['膨胀节'],
          mappings: [
            { equipment: '膨胀节类型', model: ['类型', '膨胀节类型'] },
            { equipment: '膨胀节长度', model: ['长度'] },
            { equipment: '膨胀节宽度', model: ['宽度'] },
            { equipment: '膨胀节接口长度', model: ['接口长度'] },
            { equipment: '膨胀节接口宽度', model: ['接口宽度'] }
          ]
        },
        '干渣机': {
          keywords: ['干渣机', '鳞斗', '输渣', 'conveyor'],
          equipment_prefixes: ['干渣机', '鳞斗'],
          mappings: [
            { equipment: '干渣机规格', model: ['规格', '干渣机规格', '设备规格'] },
            { equipment: '鳞斗干渣机型号', model: ['规格型号', '型号', '设备型号', '鳞斗干渣机型号'] },
            { equipment: '干渣机-倾角α(°)', model: ['倾角', '角度', '斜升角度'] },
            { equipment: '干渣机-总长（mm）', model: ['总长', '总长度'] },
            { equipment: '干渣机-水平段长（mm）', model: ['水平段长', '水平段长度'] },
            { equipment: '干渣机-斜升段长（mm）', model: ['斜升段长', '斜升段长度'] },
            { equipment: '干渣机-支腿高度（mm）', model: ['支腿高度'] }
          ]
        },
        '渣井': {
          keywords: ['渣井', '干渣井', 'well'],
          equipment_prefixes: ['渣井', '干渣井'],
          mappings: [
            { equipment: '干渣井型号', model: ['规格型号', '型号', '设备型号', '干渣井型号'] },
            { equipment: '渣井壁板下口高度 [mm]', model: ['下口标高', '井底标高', '壁板下口高度'] },
            { equipment: '渣井壁板上口高度 [mm]', model: ['壁板上口标高', '上口标高', '壁板上口高度'] },
            { equipment: '渣井壁板上口长度 [mm]', model: ['壁板上口长度', '上口长度'] },
            { equipment: '渣井左右端壁板角度 [度]', model: ['壁板角度', '左右壁板角度', '左右端壁板角度', '倾角'] }
          ]
        },
        '斜梯': {
          keywords: ['斜梯', '爬梯', '扶梯', '梯子', 'ladder'],
          equipment_prefixes: ['斜梯', '爬梯', '扶梯'],
          mappings: [
            { equipment: '斜梯内宽', model: ['内宽', '梯子内宽', '宽度'] },
            { equipment: '斜梯角度', model: ['角度', '梯子角度', '倾角'] },
            { equipment: '斜梯高度', model: ['高度', '梯子高度'] },
            { equipment: '对接平台高度', model: ['平台高度', '对接平台高度'] }
          ]
        },
        '平台': {
          keywords: ['平台', '仓顶平台', 'platform'],
          equipment_prefixes: ['平台', '仓顶平台'],
          mappings: [
            { equipment: '渣仓直径', model: ['渣仓直径', '混合直径', '直径', '仓体直径'] },
            { equipment: '渣仓顶标高', model: ['顶标高', '平台标高', '标高'] },
            { equipment: '渣仓型号', model: ['物料编码前缀', '型号', '规格型号'] },
            { equipment: '平台高度', model: ['高度', '平台高度', '标高'] },
            { equipment: '平台长度', model: ['长度', '平台长度'] },
            { equipment: '平台宽度', model: ['宽度', '平台宽度'] }
          ]
        },
        '护栏': {
          keywords: ['护栏', '围栏', '栏杆', 'railing'],
          equipment_prefixes: ['护栏', '围栏'],
          mappings: [
            { equipment: '护栏高度', model: ['高度', '护栏高度'] },
            { equipment: '护栏长度', model: ['长度', '护栏长度'] }
          ]
        }
      },
      global_attribute_mappings: [
        { equipment: '干渣井型号', model: ['干渣井型号', '规格型号', '型号'] },
        { equipment: '渣仓型号', model: ['渣仓型号', '规格型号', '型号', '物料编码前缀'] },
        { equipment: '液压破碎关断门型号', model: ['关断门型号', '规格型号', '型号'] },
        { equipment: '鳞斗干渣机型号', model: ['干渣机型号', '规格型号', '型号'] }
      ],
      // 用于后续模型搭建的通用尺寸参数：只要 manifest 里有同名/映射参数就回填，不按模型类型前缀过滤
      shared_construction_equipment: [
        '渣仓直径', '渣仓顶标高', '渣仓锥段高度', '渣仓柱段高度', '渣仓有效容积', '渣仓卸料类型',
        '渣井壁板下口高度 [mm]', '渣井壁板上口高度 [mm]', '渣井壁板上口长度 [mm]', '渣井左右端壁板角度 [度]',
        '渣井口间距 [mm]', '关断门定位距离', '锅炉梳型密封板长度 [mm]', 'L密封板自身高度 [mm]', '锅炉梳型密封板宽度 [mm]',
        '弧段侧的关断门箱体边缘距离 [mm]', '鳞斗干渣机弧段定位距离 [mm]',
        '膨胀节长度', '膨胀节宽度', '膨胀节接口长度', '膨胀节接口宽度'
      ],
      default_units: {
        '渣仓直径': 'mm',
        '渣仓顶标高': 'mm',
        '渣仓锥段高度': 'mm',
        '渣仓柱段高度': 'mm',
        '下联箱标高 [mm]': 'mm',
        '锅炉梳型密封板长度 [mm]': 'mm',
        '锅炉梳型密封板宽度 [mm]': 'mm',
        'L密封板自身高度 [mm]': 'mm',
        '渣井口间距 [mm]': 'mm',
        '关断门定位距离': 'mm',
        '渣井壁板下口高度 [mm]': 'mm',
        '渣井壁板上口高度 [mm]': 'mm',
        '渣井壁板上口长度 [mm]': 'mm',
        '渣井左右端壁板角度 [度]': '°',
        '膨胀节长度': 'mm',
        '膨胀节宽度': 'mm',
        '膨胀节接口长度': 'mm',
        '膨胀节接口宽度': 'mm'
      }
    };

    function inferEquipmentDefaultUnit(paramName, value) {
      const name = normalizeParamKey(normalizeEquipmentParamName(paramName));
      const defaults = EQUIPMENT_MODEL_PARAMETER_MAPPING.default_units || {};
      const matched = Object.entries(defaults).find(([key]) => normalizeParamKey(key) === name);
      if (matched) return matched[1];
      const split = splitAutofillValueAndUnit(value);
      if (split.unit) return split.unit;
      if (split.numeric != null && !isNaN(split.numeric) && split.numeric >= 100) return 'mm';
      return '';
    }

    function equipmentEntryQuality(entry) {
      if (!entry) return 0;
      let score = 0;
      if (entry.hasExplicitUnit) score += 20;
      if (/mm|cm|m³|m\^3|°|度/i.test(String(entry.value || ''))) score += 10;
      if (/设备选型|selection\.parameters|已编辑/.test(String(entry.source || ''))) score += 8;
      if (/候选池/.test(String(entry.source || ''))) score += 1;
      return score;
    }

    function normalizeEquipmentParamName(name) {
      const text = stripMarkdown(name).trim();
      if (!text) return '';
      const aliases = EQUIPMENT_MODEL_PARAMETER_MAPPING.legacy_name_aliases || {};
      if (aliases[text]) return aliases[text];
      const normalized = normalizeParamKey(text);
      const alias = Object.entries(aliases).find(([key]) => normalizeParamKey(key) === normalized);
      if (alias) return alias[1];
      return text;
    }

    function detectModelTypeFromContext(result) {
      const context = [
        result && result.selected_model,
        result && result.template_name,
        result && result.selected_full_node_path,
        currentSelection && currentSelection.selected_model,
        currentSelection && currentSelection.template_name,
        currentSelection && currentSelection.selected_full_node_path
      ].filter(Boolean).join(' ');
      const compact = normalizeParamKey(context);
      if (!compact) return '';
      const models = EQUIPMENT_MODEL_PARAMETER_MAPPING.models || {};
      let best = '';
      let bestScore = 0;
      Object.entries(models).forEach(([type, spec]) => {
        const keywords = (spec && spec.keywords) || [];
        let score = 0;
        keywords.forEach((keyword) => {
          const key = normalizeParamKey(keyword);
          if (key && compact.includes(key)) score += key.length;
        });
        if (score > bestScore) {
          bestScore = score;
          best = type;
        }
      });
      return best;
    }

    function equipmentFollowUpCategory(followUp) {
      const text = stripMarkdown(followUp);
      if (!text) return 'construction';
      if (/写入模型参数属性|带入图纸明细表|明细表/.test(text)) return 'attribute';
      if (/逻辑计算/.test(text)) return 'logic';
      if (/模型搭建|后续模型/.test(text)) return 'construction';
      return 'construction';
    }

    function equipmentParamBelongsToModel(equipmentParam, modelType) {
      const name = normalizeEquipmentParamName(equipmentParam);
      if (!name || !modelType) return false;
      const spec = (EQUIPMENT_MODEL_PARAMETER_MAPPING.models || {})[modelType];
      if (!spec) return false;
      const prefixes = spec.equipment_prefixes || [];
      const normalized = normalizeParamKey(name);
      if (prefixes.some((prefix) => normalized.startsWith(normalizeParamKey(prefix)))) return true;
      const mappings = spec.mappings || [];
      return mappings.some((item) => normalizeParamKey(item.equipment) === normalized);
    }

    function isSharedConstructionEquipment(paramName) {
      const name = normalizeParamKey(normalizeEquipmentParamName(paramName));
      if (!name) return false;
      const shared = EQUIPMENT_MODEL_PARAMETER_MAPPING.shared_construction_equipment || [];
      return shared.some((item) => normalizeParamKey(item) === name);
    }

    function equipmentEntryAppliesToModel(entry, modelType, modelParamName) {
      if (!entry) return false;
      const category = equipmentFollowUpCategory(entry.followUp);
      if (category === 'attribute') return true;
      if (isSharedConstructionEquipment(entry.param)) return true;
      if (modelParamName && resolveModelParamFromMapping(modelParamName, modelType)) {
        const mapped = resolveModelParamFromMapping(modelParamName, modelType);
        if (normalizeParamKey(mapped) === normalizeParamKey(entry.param)) return true;
      }
      if (!modelType) return category === 'construction' || category === 'logic';
      return equipmentParamBelongsToModel(entry.param, modelType);
    }

    function parseEquipmentRowsFromMarkdown(text) {
      const rows = [];
      const lines = String(text || '').replace(/\r\n/g, '\n').split('\n');
      for (let index = 0; index < lines.length - 2; index += 1) {
        const headers = splitMarkdownTableLine(lines[index]);
        if (!headers.length || !isMarkdownTableSeparator(lines[index + 1])) continue;
        const nameIndex = findHeaderIndex(headers, ['参数', '设备名称', '参数名称']);
        const valueIndex = findHeaderIndex(headers, ['参数值', '设备型号/参数', '型号/参数', '使用值', '数值', '值']);
        const followUpIndex = findHeaderIndex(headers, ['后续应用', '应用说明', '应用', '取得途径']);
        if (nameIndex < 0 || valueIndex < 0) continue;
        const tableRows = [];
        let rowIndex = index + 2;
        while (rowIndex < lines.length) {
          const cells = splitMarkdownTableLine(lines[rowIndex]);
          if (!cells.length) break;
          const row = {};
          headers.forEach((header, cellIndex) => {
            const key = stripMarkdown(header);
            if (key) row[key] = stripMarkdown(cells[cellIndex] || '');
          });
          const param = rowValueByHeader(row, ['参数', '设备名称', '参数名称']);
          const value = rowValueByHeader(row, ['参数值', '设备型号/参数', '型号/参数', '使用值', '数值', '值']);
          if (param && value) tableRows.push(row);
          rowIndex += 1;
        }
        if (!tableRows.length) continue;
        const hasEquipmentShape = tableRows.some((row) => {
          const followUp = rowValueByHeader(row, ['后续应用', '应用说明', '应用', '取得途径']);
          const param = rowValueByHeader(row, ['参数', '设备名称', '参数名称']);
          return /模型搭建|后续模型|写入模型参数属性|带入图纸明细表|明细表/.test(String(followUp || ''))
            || /渣仓|渣井|关断门|干渣机|膨胀节|碎渣机|卸料/.test(String(param || ''));
        });
        if (hasEquipmentShape) rows.push(...tableRows);
        index = rowIndex;
      }
      return rows;
    }

    function mergeEquipmentResultRows(existing, incoming) {
      const merged = Array.isArray(existing) ? existing.slice() : [];
      const indexByParam = new Map();
      merged.forEach((row, idx) => {
        const param = rowValueByHeader(row, ['参数', '设备名称', '参数名称']);
        if (param) indexByParam.set(normalizeParamKey(param), idx);
      });
      (incoming || []).forEach((row) => {
        const param = rowValueByHeader(row, ['参数', '设备名称', '参数名称']);
        const value = rowValueByHeader(row, ['参数值', '设备型号/参数', '型号/参数', '使用值', '数值', '值']);
        if (!param || !value) return;
        const key = normalizeParamKey(param);
        if (indexByParam.has(key)) merged[indexByParam.get(key)] = Object.assign({}, merged[indexByParam.get(key)], row);
        else {
          indexByParam.set(key, merged.length);
          merged.push(row);
        }
      });
      return merged;
    }

    function rememberEquipmentSelectionFromText(text, source) {
      const rows = parseEquipmentRowsFromMarkdown(text);
      if (!rows.length) return false;
      if (!currentSelection) currentSelection = {};
      currentSelection.equipment_result_rows = mergeEquipmentResultRows(currentSelection.equipment_result_rows, rows);
      const params = Object.assign({}, currentSelection.parameters || {});
      rows.forEach((row) => {
        const label = rowValueByHeader(row, ['参数', '设备名称', '参数名称']);
        const value = rowValueByHeader(row, ['参数值', '设备型号/参数', '型号/参数', '使用值', '数值', '值']);
        if (label && value) {
          const normalized = typeof normalizeEquipmentParamName === 'function'
            ? normalizeEquipmentParamName(label)
            : label;
          params[normalized] = value;
          params[label] = value;
        }
      });
      currentSelection.parameters = filterParameterMap(params);
      currentSelection.equipment_selection_source = source || 'markdown';
      persistEquipmentSelectionCache();
      updateJiavuActions(currentSelection);
      return true;
    }

    function rememberEquipmentSelectionFromChatDom() {
      if (!chat) return false;
      let remembered = false;
      chat.querySelectorAll('table').forEach((table) => {
        if (table.closest('[data-param-table="true"]') || table.closest('[data-fallback-param-table="true"]')) return;
        const headers = getMarkdownTableHeaders(table);
        const nameIndex = findHeaderIndex(headers, ['参数', '设备名称', '参数名称']);
        const valueIndex = findHeaderIndex(headers, ['参数值', '设备型号/参数', '型号/参数', '使用值', '数值', '值']);
        if (nameIndex < 0 || valueIndex < 0) return;
        const rows = [];
        table.querySelectorAll('tr').forEach((tr) => {
          const cells = tr.querySelectorAll('th, td');
          if (cells.length <= Math.max(nameIndex, valueIndex)) return;
          const row = {};
          headers.forEach((header, index) => {
            if (!header) return;
            const cell = cells[index];
            if (!cell) return;
            const control = cell.querySelector('input.param-table-input, select.param-table-input');
            row[header] = control
              ? normalizeEditableCellValue(control.value)
              : normalizeEditableCellValue(cell.textContent || '');
          });
          const param = rowValueByHeader(row, ['参数', '设备名称', '参数名称']);
          const value = rowValueByHeader(row, ['参数值', '设备型号/参数', '型号/参数', '使用值', '数值', '值']);
          if (!param || !value) return;
          if (['参数', '设备名称', '参数名称'].includes(stripMarkdown(param))) return;
          if (param && value) rows.push(row);
        });
        if (!rows.length) return;
        if (!currentSelection) currentSelection = {};
        currentSelection.equipment_result_rows = mergeEquipmentResultRows(currentSelection.equipment_result_rows, rows);
        const params = Object.assign({}, currentSelection.parameters || {});
        rows.forEach((row) => {
          const label = rowValueByHeader(row, ['参数', '设备名称', '参数名称']);
          const value = rowValueByHeader(row, ['参数值', '设备型号/参数', '型号/参数', '使用值', '数值', '值']);
          if (label && value) {
            const normalized = normalizeEquipmentParamName(label);
            params[normalized] = value;
            params[label] = value;
          }
        });
        currentSelection.parameters = filterParameterMap(params);
        currentSelection.equipment_selection_source = 'DOM';
        remembered = true;
      });
      if (remembered) {
        persistEquipmentSelectionCache();
        updateJiavuActions(currentSelection);
      }
      return remembered;
    }

    function persistEquipmentSelectionCache() {
      try {
        if (!currentSelection) return;
        const payload = {
          equipment_result_rows: currentSelection.equipment_result_rows || [],
          parameters: currentSelection.parameters || {},
          updated_at: Date.now()
        };
        sessionStorage.setItem('cidu_equipment_selection_cache', JSON.stringify(payload));
      } catch (_) { /* ignore quota / privacy mode */ }
    }

    function loadEquipmentSelectionCache() {
      try {
        const raw = sessionStorage.getItem('cidu_equipment_selection_cache');
        if (!raw) return null;
        const parsed = JSON.parse(raw);
        return parsed && typeof parsed === 'object' ? parsed : null;
      } catch (_) {
        return null;
      }
    }

    function collectEquipmentFromResultMeta(result, upsertEntry) {
      if (!result || typeof upsertEntry !== 'function') return;
      const markdown = String(result.answer_markdown || result.message || '');
      if (typeof parseSelectionMetaFromMarkdown === 'function') {
        const meta = parseSelectionMetaFromMarkdown(markdown);
        const equipParams = meta && meta.equipment_selection_parameters;
        if (equipParams && typeof equipParams === 'object') {
          Object.entries(equipParams).forEach(([key, value]) => {
            upsertEntry(key, value, '用于后续模型搭建', 'cidu_selection_meta');
          });
        }
      }
      if (typeof parseMarkdownParameters === 'function') {
        Object.entries(parseMarkdownParameters(markdown)).forEach(([key, value]) => {
          upsertEntry(key, value, '用于后续模型搭建', '模型需要的参数');
        });
      }
      const cache = loadEquipmentSelectionCache();
      if (cache && cache.parameters) {
        Object.entries(cache.parameters).forEach(([key, value]) => {
          upsertEntry(key, value, '用于后续模型搭建', 'sessionStorage.parameters');
        });
      }
    }

    function collectEquipmentSelectionEntries(result) {
      syncAllEditableParameterTablesToSelection();
      const entries = [];
      const entryIndexByParam = new Map();
      const upsertEntry = (param, value, followUp, source, sourceUnit) => {
        const name = normalizeEquipmentParamName(param);
        if (!name || !isValidAutofillValue(value)) return;
        const rawValue = stripMarkdown(value);
        const split = splitAutofillValueAndUnit(rawValue);
        const resolvedUnit = normalizeAutofillUnit(
          sourceUnit || parseUnitFromParameterName(name) || split.unit || inferEquipmentDefaultUnit(name, rawValue)
        );
        const entry = {
          param: name,
          value: rawValue,
          followUp: stripMarkdown(followUp || ''),
          source: source || '',
          sourceUnit: resolvedUnit,
          hasExplicitUnit: Boolean(split.unit || sourceUnit || /mm|cm|m³|m\^3|°|度/i.test(rawValue))
        };
        const key = normalizeParamKey(name);
        const prevIndex = entryIndexByParam.get(key);
        if (prevIndex == null) {
          entries.push(entry);
          entryIndexByParam.set(key, entries.length - 1);
          return;
        }
        if (equipmentEntryQuality(entry) > equipmentEntryQuality(entries[prevIndex])) {
          entries[prevIndex] = entry;
        }
      };

      collectEquipmentFromResultMeta(result, upsertEntry);

      const collectFromRows = (rows, source) => {
        (rows || []).forEach((row) => {
          const param = rowValueByHeader(row, ['参数', '设备名称', '参数名称', '参数中文名称']);
          const value = rowValueByHeader(row, ['参数值', '设备型号/参数', '型号/参数', '使用值', '数值', '值']);
          const followUp = rowValueByHeader(row, ['后续应用', '应用说明', '应用', '取得途径']);
          const unit = rowValueByHeader(row, ['单位']);
          if (param && value) upsertEntry(param, value, followUp, source, unit);
        });
      };

      const collectFromChatDomTables = () => {
        rememberEquipmentSelectionFromChatDom();
      };

      rememberEquipmentSelectionFromChatDom();

      if (currentSelection && Array.isArray(currentSelection.equipment_result_rows)) {
        collectFromRows(currentSelection.equipment_result_rows, '设备选型结果(已编辑)');
      }

      const cache = loadEquipmentSelectionCache();
      if (cache && Array.isArray(cache.equipment_result_rows)) {
        collectFromRows(cache.equipment_result_rows, 'sessionStorage');
      }

      const conversation = activeConversation();
      const messages = conversation && Array.isArray(conversation.messages) ? conversation.messages : [];
      messages.forEach((message) => {
        const text = messageTextForParameterScan(message);
        const lines = String(text || '').replace(/\r\n/g, '\n').split('\n');
        for (let index = 0; index < lines.length - 2; index += 1) {
          const headers = splitMarkdownTableLine(lines[index]);
          if (!headers.length || !isMarkdownTableSeparator(lines[index + 1])) continue;
          const nameIndex = findHeaderIndex(headers, ['参数', '参数名称', '设备名称']);
          const valueIndex = findHeaderIndex(headers, ['参数值', '设备型号/参数', '型号/参数', '使用值', '数值']);
          const followUpIndex = findHeaderIndex(headers, ['后续应用', '应用说明', '应用', '取得途径']);
          if (nameIndex < 0 || valueIndex < 0) continue;
          let rowIndex = index + 2;
          while (rowIndex < lines.length) {
            const cells = splitMarkdownTableLine(lines[rowIndex]);
            if (!cells.length) break;
            upsertEntry(
              cells[nameIndex],
              cells[valueIndex],
              followUpIndex >= 0 ? cells[followUpIndex] : '',
              '设备选型结果'
            );
            rowIndex += 1;
          }
          index = rowIndex;
        }
      });

      if (result) {
        const resultText = [
          result.answer_markdown || '',
          result.message || ''
        ].filter(Boolean).join('\n\n');
        if (resultText.trim()) {
          const lines = resultText.replace(/\r\n/g, '\n').split('\n');
          for (let index = 0; index < lines.length - 2; index += 1) {
            const headers = splitMarkdownTableLine(lines[index]);
            if (!headers.length || !isMarkdownTableSeparator(lines[index + 1])) continue;
            const nameIndex = findHeaderIndex(headers, ['参数', '参数名称', '设备名称']);
            const valueIndex = findHeaderIndex(headers, ['参数值', '设备型号/参数', '型号/参数', '使用值', '数值']);
            const followUpIndex = findHeaderIndex(headers, ['后续应用', '应用说明', '应用', '取得途径']);
            if (nameIndex < 0 || valueIndex < 0) continue;
            let rowIndex = index + 2;
            while (rowIndex < lines.length) {
              const cells = splitMarkdownTableLine(lines[rowIndex]);
              if (!cells.length) break;
              upsertEntry(
                cells[nameIndex],
                cells[valueIndex],
                followUpIndex >= 0 ? cells[followUpIndex] : '',
                '当前响应'
              );
              rowIndex += 1;
            }
            index = rowIndex;
          }
        }
      }

      collectFromChatDomTables();

      if (currentSelection && currentSelection.parameters) {
        Object.entries(currentSelection.parameters).forEach(([key, value]) => {
          upsertEntry(key, value, '用于后续模型搭建', 'selection.parameters');
        });
      }

      if (typeof collectAutofillCandidates === 'function') {
        const candidates = collectAutofillCandidates(result || {});
        candidates.forEach((candidate) => {
          const rawValue = String(candidate.value == null ? '' : candidate.value).trim();
          const withUnit = candidate.sourceUnit
            && /^\d/.test(rawValue)
            && !/[a-zA-Z×°]/.test(rawValue)
            ? rawValue + candidate.sourceUnit
            : rawValue;
          upsertEntry(candidate.key, withUnit, '', candidate.source || '候选池', candidate.sourceUnit);
        });
      }

      return entries;
    }

    function resolveModelParamFromMapping(paramName, modelType) {
      const target = comparableAutofillName(paramName);
      if (!target) return null;
      const models = EQUIPMENT_MODEL_PARAMETER_MAPPING.models || {};
      const specs = [];
      if (modelType && models[modelType]) specs.push(models[modelType]);
      Object.values(models).forEach((spec) => {
        if (spec && !specs.includes(spec)) specs.push(spec);
      });
      (EQUIPMENT_MODEL_PARAMETER_MAPPING.global_attribute_mappings || []).forEach((item) => {
        specs.push({ mappings: [item] });
      });
      for (const spec of specs) {
        const mappings = (spec && spec.mappings) || [];
        for (const item of mappings) {
          const modelNames = Array.isArray(item.model) ? item.model : [item.model];
          const matched = modelNames.some((name) => {
            const key = comparableAutofillName(name);
            return key && (target === key || target.includes(key) || key.includes(target));
          });
          if (matched) return item.equipment;
        }
      }
      return null;
    }

    function findEquipmentEntryForModelParam(param, entries, modelType) {
      if (!param || !entries || !entries.length) return null;
      const targetKey = comparableAutofillName(param.name);
      if (targetKey) {
        const direct = entries.find((entry) => {
          const sourceKey = comparableAutofillName(entry.param);
          if (!sourceKey) return false;
          if (targetKey === sourceKey) return true;
          const mapped = resolveModelParamFromMapping(param.name, modelType);
          if (mapped && normalizeParamKey(mapped) === normalizeParamKey(entry.param)) return true;
          return targetKey.includes(sourceKey) || sourceKey.includes(targetKey);
        });
        if (direct) return direct;
      }
      const equipmentName = resolveModelParamFromMapping(param.name, modelType);
      if (equipmentName) {
        const exact = entries.find((entry) => normalizeParamKey(entry.param) === normalizeParamKey(equipmentName));
        if (exact) return exact;
      }
      const target = comparableAutofillName(param.name);
      return entries.find((entry) => {
        if (!equipmentEntryAppliesToModel(entry, modelType, param.name)) return false;
        const source = comparableAutofillName(entry.param);
        return source && (target === source || target.includes(source) || source.includes(target));
      }) || null;
    }

    function applyExplicitEquipmentMapping(model, result) {
      if (!model || !Array.isArray(model.params)) return { applied: {}, debug: { entries: 0 } };
      rememberEquipmentSelectionFromChatDom();
      const modelType = detectModelTypeFromContext(result);
      const entries = collectEquipmentSelectionEntries(result);
      const applied = {};
      if (!entries.length) {
        return { applied, debug: { entries: 0, modelType: modelType || '' } };
      }
      model.params.forEach((param) => {
        if (!param || param.grid == null) return;
        if (!model.isPotentiallyVisible(param) && !model.isVisible(param)) return;
        if (model.isUserTouched && model.isUserTouched(param.grid)) return;
        const entry = findEquipmentEntryForModelParam(param, entries, modelType);
        if (!entry) return;
        const entrySplit = splitAutofillValueAndUnit(entry.value);
        const sourceUnit = entry.sourceUnit
          || parseUnitFromParameterName(entry.param)
          || entrySplit.unit
          || inferEquipmentDefaultUnit(entry.param, entry.value);
        const value = convertAutofillValue(
          entry.value,
          sourceUnit,
          param.unit || parseUnitFromParameterName(param.name)
        );
        const resolvedValue = resolveAutofillOptionValue(model, param, value, { preserveUnlisted: true });
        if (!isValidAutofillValue(resolvedValue)) return;
        model.setValue(param.grid, resolvedValue, { silent: true, userTouched: true });
        applied[param.name || String(param.grid)] = {
          value: resolvedValue,
          sourceValue: entry.value,
          source: entry.param,
          sourceUnit: sourceUnit || '',
          mapping: 'explicit',
          modelType: modelType || ''
        };
      });
      if (Object.keys(applied).length && model.resolveAllDefaults) {
        model.resolveAllDefaults();
      }
      return {
        applied,
        debug: {
          entries: entries.length,
          modelType: modelType || '',
          sample: entries.slice(0, 3).map((entry) => entry.param + '=' + entry.value).join('; ')
        }
      };
    }
