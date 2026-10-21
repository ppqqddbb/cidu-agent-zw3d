    // =========================================================
    // “开始计算”结果 -> 设计模型参数桥。
    //
    // 输入参数核对与设备选型结果必须来自同一条、最近一次“开始计算”响应。
    // 两张表以一个原子快照保存；其他响应、历史候选池和模型确认响应均无权覆盖。
    // =========================================================
    const ciduParameterBridge = (() => {
      const STORAGE_VERSION = 'v3';
      const STORAGE_PREFIX = 'cidu.parameterBridge.' + STORAGE_VERSION + ':';
      const memoryStates = new Map();
      const pendingRequests = new Map();
      const aliases = {
        '锅炉下联箱标高': '下联箱标高 [mm]',
        '锅炉下联箱标高 T1': '下联箱标高 [mm]',
        '下联箱梳形板长度': '锅炉梳型密封板长度 [mm]',
        '下联箱梳型密封板长度': '锅炉梳型密封板长度 [mm]',
        '下联箱梳形板宽度': '锅炉梳型密封板宽度 [mm]',
        '下联箱梳型密封板宽度': '锅炉梳型密封板宽度 [mm]',
        '关断门箱体长度': '锅炉梳型密封板长度 [mm]',
        '关断门箱体宽度': '锅炉梳型密封板宽度 [mm]',
        '关断门箱体高度': 'L密封板自身高度 [mm]',
        '关断门口间距': '渣井口间距 [mm]',
        '渣井下口标高': '渣井壁板下口高度 [mm]',
        '渣井壁板上口标高': '渣井壁板上口高度 [mm]',
        '渣井左右壁板角度': '渣井左右端壁板角度 [度]',
        '干渣仓仓顶是否有切角': '干渣仓仓顶是否切角',
        '渣仓仓体仓顶是否切角': '干渣仓仓顶是否切角',
        '渣仓仓体仓顶是否有切角': '干渣仓仓顶是否切角'
      };

      function strip(value) {
        let text = String(value == null ? '' : value).trim();
        text = text.replace(/[`*_]/g, '').trim();
        return text;
      }

      function validValue(value) {
        const text = strip(value);
        return Boolean(text) && ![
          '-', 'null', 'none', 'undefined', 'nan', 'n/a', 'na',
          '未识别', '待补充', '待确认', '未知', '未找到明确依据', '缺失'
        ].includes(text.toLowerCase());
      }

      function normalizeUnit(value) {
        let text = strip(value);
        if (!text || text === '-') return '';
        if (typeof text.normalize === 'function') text = text.normalize('NFKC');
        return text
          .replace(/[（）()\[\]【】\s]/g, '')
          .replace(/毫米/g, 'mm')
          .replace(/厘米/g, 'cm')
          .replace(/立方米|m³|m\^3/gi, 'm3')
          .replace(/小时/g, 'h')
          .replace(/度/g, '°')
          .toLowerCase();
      }

      function unitFromName(name) {
        const matches = [...String(name || '').matchAll(/[（(【\[]\s*([^）)】\]]+)\s*[）)】\]]/g)];
        return matches.length ? normalizeUnit(matches[matches.length - 1][1]) : '';
      }

      function splitValue(value) {
        const raw = strip(value);
        const match = raw.match(/^\s*([-+]?\d+(?:\.\d+)?)\s*([^\d\s]+)?\s*$/);
        if (!match) return { raw, value: raw, numeric: null, unit: '' };
        return {
          raw,
          value: match[1],
          numeric: Number(match[1]),
          unit: normalizeUnit(match[2] || '')
        };
      }

      function isSiloTopCutParameter(param) {
        if (!param) return false;
        return canonicalKeyRaw(aliasName(param.name || ''))
          === canonicalKeyRaw('干渣仓仓顶是否切角');
      }

      function siloTopCutValueForModel(value, param) {
        const raw = strip(value).replace(/\s+/g, '');
        if (!raw) return '';
        let semantic = '';
        if (/不切角|不设切角|不需要切角/.test(raw) || raw === '否') semantic = 'no-cut';
        else if (/切角|需要切角/.test(raw) || raw === '是') semantic = 'cut';
        if (!semantic) return strip(value);

        // 旧模型清单可能仍以“是/否”为内部枚举。界面显示统一为“切角/不切角”，
        // 但写入模型时沿用清单真实枚举，避免破坏依赖 <grid>=是/否 的模型公式。
        const valueList = String(param && param.value_list || '');
        const usesBooleanEnum = valueList.includes('是') && valueList.includes('否') && !valueList.includes('切角');
        if (usesBooleanEnum) return semantic === 'cut' ? '是' : '否';
        return semantic === 'cut' ? '切角' : '不切角';
      }

      function aliasName(name) {
        let text = strip(name);
        if (!text) return '';
        if (typeof normalizeEquipmentParamName === 'function') {
          const normalized = normalizeEquipmentParamName(text);
          if (normalized) text = normalized;
        }
        if (aliases[text]) return aliases[text];
        const key = canonicalKeyRaw(text);
        const matched = Object.entries(aliases).find(([source]) => canonicalKeyRaw(source) === key);
        return matched ? matched[1] : text;
      }

      function canonicalKeyRaw(value) {
        let text = strip(value);
        if (typeof text.normalize === 'function') text = text.normalize('NFKC');
        return text
          .replace(/[【［]/g, '[')
          .replace(/[】］]/g, ']')
          .replace(/（/g, '(')
          .replace(/）/g, ')')
          .replace(/梳形/g, '梳型')
          .replace(/\s+/g, '')
          .toLowerCase();
      }

      function displayName(name, unit) {
        const resolved = aliasName(name);
        const cleanUnit = strip(unit);
        if (!resolved || !cleanUnit || /[\[【［（(]/.test(resolved)) return resolved;
        return resolved + ' [' + cleanUnit + ']';
      }

      function bareName(name) {
        return strip(name).replace(/\s*[（(【\[][^）)】\]]+[）)】\]]\s*$/, '').trim();
      }

      function conversationId() {
        try {
          const conversation = typeof activeConversation === 'function' ? activeConversation() : null;
          const id = conversation && (conversation.id || conversation.ciduConversationId);
          if (id) return String(id);
        } catch (_) {}
        try {
          if (typeof activeConversationId !== 'undefined' && activeConversationId) {
            return String(activeConversationId);
          }
        } catch (_) {}
        return 'global';
      }

      function emptyState(id) {
        return {
          version: STORAGE_VERSION,
          conversation_id: id,
          revision_counter: 0,
          calculation_revision: 0,
          calculation_id: '',
          calculation_signature: '',
          calculation_source: '',
          calculation_updated_at: 0,
          extraction: { rows: [], entries: [], updated_at: 0, revision: 0, signature: '', source: '' },
          equipment: { rows: [], entries: [], updated_at: 0, revision: 0, signature: '', source: '' }
        };
      }

      function storageKey(id) {
        return STORAGE_PREFIX + id;
      }

      function loadState() {
        const id = conversationId();
        if (memoryStates.has(id)) return memoryStates.get(id);
        let state = null;
        try {
          const raw = sessionStorage.getItem(storageKey(id));
          const parsed = raw ? JSON.parse(raw) : null;
          if (parsed && parsed.version === STORAGE_VERSION && parsed.conversation_id === id) {
            state = parsed;
          }
        } catch (_) {}
        if (!state) state = emptyState(id);
        state.extraction = state.extraction || emptyState(id).extraction;
        state.equipment = state.equipment || emptyState(id).equipment;
        state.calculation_revision = Number(state.calculation_revision || 0);
        state.calculation_id = String(state.calculation_id || '');
        state.calculation_signature = String(state.calculation_signature || '');
        state.calculation_source = String(state.calculation_source || '');
        state.calculation_updated_at = Number(state.calculation_updated_at || 0);
        state.revision_counter = Math.max(
          Number(state.revision_counter || 0),
          state.calculation_revision,
          Number(state.extraction.revision || 0),
          Number(state.equipment.revision || 0)
        );
        state.extraction.revision = Number(state.extraction.revision || 0);
        state.equipment.revision = Number(state.equipment.revision || 0);
        state.extraction.signature = String(state.extraction.signature || '');
        state.equipment.signature = String(state.equipment.signature || '');
        state.extraction.entries = entriesFromRows(state.extraction.rows, 'extraction');
        state.equipment.entries = entriesFromRows(state.equipment.rows, 'equipment');
        memoryStates.set(id, state);
        return state;
      }

      function persist(state) {
        const serializable = {
          version: state.version,
          conversation_id: state.conversation_id,
          revision_counter: Number(state.revision_counter || 0),
          calculation_revision: Number(state.calculation_revision || 0),
          calculation_id: String(state.calculation_id || ''),
          calculation_signature: String(state.calculation_signature || ''),
          calculation_source: String(state.calculation_source || ''),
          calculation_updated_at: Number(state.calculation_updated_at || 0),
          extraction: Object.assign({}, state.extraction, { entries: [] }),
          equipment: Object.assign({}, state.equipment, { entries: [] })
        };
        try { sessionStorage.setItem(storageKey(state.conversation_id), JSON.stringify(serializable)); } catch (_) {}
      }

      function normalizedHeader(value) {
        return canonicalKeyRaw(value).replace(/[/:_-]/g, '');
      }

      function valueByHeader(row, names) {
        const accepted = names.map(normalizedHeader);
        for (const [key, value] of Object.entries(row || {})) {
          const normalized = normalizedHeader(key);
          if (accepted.some((name) => normalized === name || normalized.includes(name))) {
            if (strip(value)) return value;
          }
        }
        return '';
      }

      function normalizeRows(rows) {
        return (rows || []).filter((row) => row && typeof row === 'object').map((row) => Object.assign({}, row));
      }

      function rowsSignature(rows) {
        const normalized = normalizeRows(rows).map((row) => {
          const ordered = {};
          Object.keys(row).sort().forEach((key) => { ordered[key] = strip(row[key]); });
          return ordered;
        });
        try { return JSON.stringify(normalized); } catch (_) { return String(normalized); }
      }

      function nextRevision(state) {
        state.revision_counter = Math.max(
          Number(state.revision_counter || 0),
          Number(state.calculation_revision || 0),
          Number(state.extraction && state.extraction.revision || 0),
          Number(state.equipment && state.equipment.revision || 0)
        ) + 1;
        return state.revision_counter;
      }

      function entryFromExtractionRow(row, sequence) {
        const identifier = valueByHeader(row, ['参数标识', '参数ID', '参数id']);
        const name = valueByHeader(row, ['参数中文名称', '参数名称', '中文名称', '参数名']);
        const value = valueByHeader(row, ['数值', '参数值', '提取数值', '识别结果', '使用值', '当前值', '值']);
        const unit = valueByHeader(row, ['单位']);
        if (!name || !validValue(value)) return null;
        return makeEntry(name, value, unit, 'parameter-extraction', 200, sequence, identifier);
      }

      function entryFromEquipmentRow(row, sequence) {
        const name = valueByHeader(row, ['参数', '参数名称', '设备名称']);
        const value = valueByHeader(row, ['参数值', '设备型号/参数', '型号/参数', '使用值', '数值', '值']);
        const unit = valueByHeader(row, ['单位']);
        if (!name || !validValue(value)) return null;
        return makeEntry(name, value, unit, 'equipment-selection', 300, sequence, '');
      }

      function makeEntry(name, value, unit, source, priority, sequence, identifier) {
        const split = splitValue(value);
        const sourceUnit = normalizeUnit(unit) || unitFromName(name) || split.unit;
        const fullName = displayName(name, sourceUnit || unit);
        const normalizedName = aliasName(fullName || name);
        return {
          name: normalizedName,
          original_name: strip(name),
          identifier: strip(identifier),
          value: strip(value),
          source_unit: sourceUnit,
          source,
          priority,
          sequence,
          full_key: canonicalKeyRaw(normalizedName),
          bare_key: canonicalKeyRaw(bareName(normalizedName))
        };
      }

      function entriesFromRows(rows, kind) {
        const entries = [];
        normalizeRows(rows).forEach((row, index) => {
          const entry = kind === 'equipment'
            ? entryFromEquipmentRow(row, index)
            : entryFromExtractionRow(row, index);
          if (entry) entries.push(entry);
        });
        return entries;
      }

      function syncSelection(state) {
        try {
          if (typeof currentSelection === 'undefined') return;
          if (!currentSelection) currentSelection = {};
          currentSelection.parameter_extraction_rows = normalizeRows(state.extraction.rows);
          currentSelection.equipment_result_rows = normalizeRows(state.equipment.rows);
          currentSelection.parameter_extraction_revision = Number(state.calculation_revision || 0);
          currentSelection.equipment_result_revision = Number(state.calculation_revision || 0);
          currentSelection.calculation_snapshot_revision = Number(state.calculation_revision || 0);
          currentSelection.calculation_snapshot_id = String(state.calculation_id || '');
          currentSelection.parameter_bridge_version = STORAGE_VERSION;
        } catch (_) {}
      }

      function commitCalculationRows(extractionRows, equipmentRows, source = 'calculation-response') {
        const extraction = normalizeRows(extractionRows);
        const equipment = normalizeRows(equipmentRows);
        const state = loadState();
        const signature = rowsSignature(extraction) + '\n---equipment---\n' + rowsSignature(equipment);
        if (signature === state.calculation_signature) {
          syncSelection(state);
          return false;
        }
        const revision = nextRevision(state);
        const updatedAt = Date.now();
        state.calculation_revision = revision;
        state.calculation_id = state.conversation_id + ':' + String(revision);
        state.calculation_signature = signature;
        state.calculation_source = source;
        state.calculation_updated_at = updatedAt;
        state.extraction = {
          rows: extraction,
          entries: entriesFromRows(extraction, 'extraction'),
          updated_at: updatedAt,
          revision,
          signature: rowsSignature(extraction),
          source
        };
        state.equipment = {
          rows: equipment,
          entries: entriesFromRows(equipment, 'equipment'),
          updated_at: updatedAt,
          revision,
          signature: rowsSignature(equipment),
          source
        };
        persist(state);
        syncSelection(state);
        return true;
      }

      function splitMarkdownLine(line) {
        const text = String(line || '').trim();
        if (!text.startsWith('|') || !text.endsWith('|')) return [];
        return text.slice(1, -1).split('|').map((cell) => strip(cell.replace(/\\\|/g, '|')));
      }

      function separatorLine(line) {
        const cells = splitMarkdownLine(line);
        return cells.length > 1 && cells.every((cell) => /^:?-+:?$/.test(cell.replace(/\s+/g, '')));
      }

      function parseMarkdownTables(markdown) {
        const lines = String(markdown || '').replace(/\r\n/g, '\n').split('\n');
        const tables = [];
        let section = '';
        for (let index = 0; index < lines.length; index += 1) {
          const heading = lines[index].match(/^\s*#{1,6}\s+(.+?)\s*$/);
          if (heading) section = strip(heading[1]);
          const headers = splitMarkdownLine(lines[index]);
          if (!headers.length || !separatorLine(lines[index + 1])) continue;
          const rows = [];
          let rowIndex = index + 2;
          while (rowIndex < lines.length) {
            const cells = splitMarkdownLine(lines[rowIndex]);
            if (!cells.length) break;
            const row = {};
            headers.forEach((header, cellIndex) => {
              if (header) row[header] = cells[cellIndex] || '';
            });
            if (Object.values(row).some((value) => strip(value))) rows.push(row);
            rowIndex += 1;
          }
          tables.push({ section, headers, rows });
          index = rowIndex - 1;
        }
        return tables;
      }

      function isExtractionTable(table) {
        const headers = (table && table.headers || []).map(normalizedHeader);
        return headers.some((header) => header === normalizedHeader('参数标识'))
          && headers.some((header) => ['参数中文名称', '参数名称', '中文名称'].map(normalizedHeader).includes(header))
          && headers.some((header) => ['数值', '参数值', '提取数值', '识别结果', '使用值'].map(normalizedHeader).includes(header));
      }

      function isEquipmentTable(table) {
        const headers = (table && table.headers || []).map(normalizedHeader);
        const hasName = headers.some((header) => ['参数', '参数名称', '设备名称'].map(normalizedHeader).includes(header));
        const hasValue = headers.some((header) => ['参数值', '设备型号/参数', '型号/参数', '数值', '值'].map(normalizedHeader).includes(header));
        return hasName && hasValue && (
          String(table && table.section || '').includes('设备选型结果')
          || headers.includes(normalizedHeader('后续应用'))
          || headers.includes(normalizedHeader('取得途径'))
        );
      }

      function calculationRowsFromMarkdown(markdown) {
        let extraction = [];
        let equipment = [];
        parseMarkdownTables(markdown).forEach((table) => {
          if (isExtractionTable(table) && table.rows.length) extraction = table.rows;
          if (isEquipmentTable(table) && table.rows.length) equipment = table.rows;
        });
        return { extraction, equipment };
      }

      function isCalculationPrompt(prompt) {
        const compact = String(prompt || '').replace(/\s+/g, '');
        return /开始计算|公式计算|进行计算|确认计算/.test(compact);
      }

      function resultMarkdown(result) {
        if (!result) return '';
        return String(
          result.answer_markdown
          || result.message
          || result.equipment_selection_markdown
          || ''
        );
      }

      function commitCalculationResult(result, source = 'calculation-response') {
        const markdownRows = calculationRowsFromMarkdown(resultMarkdown(result));
        const extraction = markdownRows.extraction.length
          ? markdownRows.extraction
          : normalizeRows(result && result.parameter_extraction_rows);
        const equipment = markdownRows.equipment.length
          ? markdownRows.equipment
          : normalizeRows(result && result.equipment_result_rows);
        return commitCalculationRows(extraction, equipment, source);
      }

      function beginRequest(prompt) {
        const id = conversationId();
        pendingRequests.set(id, {
          prompt: String(prompt || ''),
          is_calculation: isCalculationPrompt(prompt),
          started_at: Date.now()
        });
      }

      function completeRequest(result) {
        const id = conversationId();
        const pending = pendingRequests.get(id);
        if (!pending) return false;
        pendingRequests.delete(id);
        if (!pending.is_calculation) return false;
        return commitCalculationResult(result || {}, 'latest-start-calculation-response');
      }

      function restoreLatestCalculation(messages) {
        let waitingForResponse = null;
        let latest = null;
        (Array.isArray(messages) ? messages : []).forEach((message) => {
          if (!message) return;
          if (message.role === 'user') {
            waitingForResponse = { is_calculation: isCalculationPrompt(message.text || '') };
            return;
          }
          if (!['result', 'assistant'].includes(message.role) || !waitingForResponse) return;
          if (waitingForResponse.is_calculation) {
            latest = message.payload || { answer_markdown: message.text || '' };
          }
          waitingForResponse = null;
        });
        if (!latest) {
          reset();
          syncSelection(loadState());
          return false;
        }
        return commitCalculationResult(latest, 'conversation-latest-start-calculation');
      }

      function ingestCalculationMarkdown(markdown, source = 'explicit-calculation-response') {
        return commitCalculationResult({ answer_markdown: markdown }, source);
      }

      function findEntry(entries, fullKey, bareKey, targetUnit) {
        const exact = (entries || []).filter((entry) => entry.full_key === fullKey);
        if (exact.length) return exact.sort((a, b) => b.sequence - a.sequence)[0];
        const bare = (entries || []).filter((entry) => entry.bare_key === bareKey);
        const unitMatched = bare.filter((entry) => {
          if (!targetUnit || !entry.source_unit) return true;
          return normalizeUnit(entry.source_unit) === normalizeUnit(targetUnit);
        });
        const candidates = unitMatched.length ? unitMatched : bare;
        if (!candidates.length) return null;
        const distinct = new Set(candidates.map((entry) => strip(entry.value)));
        if (distinct.size > 1) return null;
        return candidates.sort((a, b) => b.sequence - a.sequence)[0];
      }

      function entryForModelParam(param) {
        if (!param) return null;
        const targetName = displayName(param.name, param.unit);
        const fullKey = canonicalKeyRaw(targetName);
        const bareKey = canonicalKeyRaw(bareName(targetName));
        const state = loadState();
        const equipment = findEntry(state.equipment.entries, fullKey, bareKey, param.unit);
        const extraction = findEntry(state.extraction.entries, fullKey, bareKey, param.unit);
        // 两张表来自同一个计算快照；同名时设备选型结果代表计算后的最终值。
        if (equipment && extraction) return equipment;
        return equipment || extraction || null;
      }

      function valueForModel(entry, param) {
        if (!entry) return '';
        if (isSiloTopCutParameter(param)) {
          return siloTopCutValueForModel(entry.value, param);
        }
        const split = splitValue(entry.value);
        if (split.numeric == null || Number.isNaN(split.numeric)) return strip(entry.value);
        const source = normalizeUnit(entry.source_unit || split.unit);
        const target = normalizeUnit(param && (param.unit || unitFromName(param.name)));
        if (!source || !target || source === target) return split.value;
        const lengthToMeter = { mm: 0.001, cm: 0.01, m: 1 };
        if (Object.prototype.hasOwnProperty.call(lengthToMeter, source)
            && Object.prototype.hasOwnProperty.call(lengthToMeter, target)) {
          return String(Number((split.numeric * lengthToMeter[source] / lengthToMeter[target]).toFixed(6)));
        }
        return split.value;
      }

      function applyDesignModel(model, result) {
        // result 是“设计模型/模型确认”响应，不是“开始计算”响应，严禁从中取值。
        // 所有自动回填只读取 completeRequest/restoreLatestCalculation 保存的原子快照。
        void result;
        const applied = {};
        if (!model || !Array.isArray(model.params)) return { applied, debug: { matched: 0 } };
        model.params.forEach((param) => {
          if (!param || param.grid == null) return;
          if (model.isPotentiallyVisible && !model.isPotentiallyVisible(param)
              && model.isVisible && !model.isVisible(param)) return;
          const entry = entryForModelParam(param);
          if (!entry) return;
          const value = valueForModel(entry, param);
          if (!validValue(value)) return;
          model.setValue(param.grid, value, { silent: true, userTouched: true });
          applied[param.name || String(param.grid)] = {
            value,
            sourceValue: entry.value,
            source: entry.name,
            sourceType: entry.source,
            priority: entry.priority,
            mapping: 'parameter-bridge-exact-name'
          };
        });
        if (Object.keys(applied).length && model.resolveAllDefaults) model.resolveAllDefaults();
        if (typeof syncAutofilledModelParametersToSelection === 'function') {
          syncAutofilledModelParametersToSelection(model, true);
        }
        syncSelection(loadState());
        return {
          applied,
          debug: {
            matched: Object.keys(applied).length,
            extraction: loadState().extraction.entries.length,
            equipment: loadState().equipment.entries.length,
            calculation_revision: Number(loadState().calculation_revision || 0),
            calculation_id: String(loadState().calculation_id || '')
          }
        };
      }

      function setControlValue(row, value) {
        if (!row || !row.querySelector) return false;
        const expected = String(value == null ? '' : value);
        const control = row.querySelector('input.param-table-input, select.param-table-input');
        if (control) {
          if (String(control.tagName || '').toUpperCase() === 'SELECT') {
            const exists = Array.from(control.options || []).some((option) => String(option.value) === expected);
            if (!exists) {
              const doc = control.ownerDocument || (typeof document !== 'undefined' ? document : null);
              if (doc && doc.createElement) {
                const option = doc.createElement('option');
                option.value = expected;
                option.textContent = expected;
                control.insertBefore(option, control.firstChild || null);
              }
            }
          }
          control.value = expected;
          return String(control.value) === expected;
        }
        const readonly = row.querySelector('.param-table-readonly');
        if (readonly) {
          readonly.textContent = expected || '—';
          return true;
        }
        return false;
      }

      function applyRenderedTable(container, model, applied) {
        if (!container || !container.querySelectorAll || !model) return 0;
        let count = 0;
        Array.from(container.querySelectorAll('tr[data-grid]') || []).forEach((row) => {
          const grid = Number(row && row.dataset ? row.dataset.grid : NaN);
          const param = model.byGrid && model.byGrid.get
            ? model.byGrid.get(grid)
            : model.params.find((item) => item && Number(item.grid) === grid);
          if (!param) return;
          const info = applied && applied[param.name || String(param.grid)];
          if (!info) return;
          if (String(model.getValue(param.grid)) !== String(info.value)) {
            model.setValue(param.grid, info.value, { silent: true, userTouched: true });
          }
          if (setControlValue(row, info.value)) count += 1;
          const remark = row.querySelector('.param-table-remark');
          if (remark) {
            const base = String(remark.textContent || '').replace(/(?:[；;]\s*)?回填自[\s\S]*$/, '').trim();
            remark.textContent = (base ? base + '；' : '')
              + '回填自 ' + info.source + '=' + info.sourceValue;
          }
        });
        return count;
      }

      function payload() {
        const state = loadState();
        return {
          parameter_bridge_version: STORAGE_VERSION,
          calculation_snapshot_revision: String(Number(state.calculation_revision || 0)),
          calculation_snapshot_id: String(state.calculation_id || ''),
          parameter_extraction_revision: String(Number(state.calculation_revision || 0)),
          equipment_result_revision: String(Number(state.calculation_revision || 0)),
          parameter_extraction_rows: normalizeRows(state.extraction.rows),
          equipment_result_rows: normalizeRows(state.equipment.rows)
        };
      }

      function reset() {
        const id = conversationId();
        memoryStates.delete(id);
        pendingRequests.delete(id);
        try { sessionStorage.removeItem(storageKey(id)); } catch (_) {}
      }

      return {
        beginRequest,
        completeRequest,
        restoreLatestCalculation,
        ingestCalculationMarkdown,
        commitCalculationRows,
        entryForModelParam,
        valueForModel,
        applyDesignModel,
        applyRenderedTable,
        payload,
        reset,
        debugState: loadState,
        canonicalKey: canonicalKeyRaw,
        displayName
      };
    })();
