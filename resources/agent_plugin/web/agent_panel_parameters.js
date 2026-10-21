    // 从“开始计算”的设备选型结果、参数提取结果、历史对话中抽取参数池，
    // 再按当前模型 manifest.params 的参数名称/单位自动回填到“参数说明”表格。
    function normalizeAutofillUnit(unit) {
      let text = String(unit || '').trim();
      if (!text) return '';
      text = text
        .replace(/[（）()\[\]【】\s]/g, '')
        .replace(/毫米/g, 'mm')
        .replace(/厘米/g, 'cm')
        .replace(/米/g, 'm')
        .replace(/立方米|m³|m\^3/gi, 'm3')
        .replace(/吨/g, 't')
        .replace(/小时/g, 'h')
        .replace(/时/g, 'h')
        .replace(/度/g, '°')
        .toLowerCase();
      return text;
    }

    function parseUnitFromParameterName(name) {
      const units = [];
      const pattern = /[（(【\[]\s*([^）)】\]]{1,20})\s*[）)】\]]/g;
      let match;
      while ((match = pattern.exec(String(name || ''))) !== null) {
        const unit = normalizeAutofillUnit(match[1]);
        if (unit) units.push(unit);
      }
      return units.length ? units[units.length - 1] : '';
    }

    function splitAutofillValueAndUnit(value) {
      const raw = stripMarkdown(value).trim();
      const match = raw.match(/^\s*([-+]?\d+(?:\.\d+)?)\s*([^\d\s]+)?\s*$/);
      if (!match) return { value: raw, unit: '', numeric: null };
      return {
        value: match[1],
        unit: normalizeAutofillUnit(match[2] || ''),
        numeric: parseFloat(match[1])
      };
    }

    function isValidAutofillValue(value) {
      const text = stripMarkdown(value).trim();
      return Boolean(text) && !['null', 'none', 'undefined', '待填写', '未找到', '-'].includes(text.toLowerCase());
    }

    function addAutofillCandidate(candidates, key, value, options = {}) {
      const name = stripMarkdown(key);
      if (!name || isIgnoredParameterName(name) || !isValidAutofillValue(value)) return;
      const split = splitAutofillValueAndUnit(value);
      const source = options.source || '';
      candidates.push({
        key: name,
        canonicalKey: canonicalAutofillKey(name),
        canonicalName: canonicalAutofillDisplayName(name),
        value: split.value || stripMarkdown(value),
        sourceUnit: normalizeAutofillUnit(options.unit) || parseUnitFromParameterName(name) || split.unit,
        source,
        kind: options.kind || '',
        scoreBias: Number(options.scoreBias || 0),
        sourceRank: Number(options.sourceRank != null ? options.sourceRank : autofillSourceRank(source))
      });
    }

    function rowValueByHeader(row, names) {
      const entries = Object.entries(row || {});
      for (const [key, value] of entries) {
        const normalized = normalizeParamKey(key);
        if (names.some((name) => normalized.includes(normalizeParamKey(name)))) {
          return value;
        }
      }
      return '';
    }

    const AUTOFILL_VALUE_HEADERS = ['使用值', '数值', '参数值', '当前值', '值'];
    let pendingEquipmentResultOverrides = [];
    let pendingParameterResultOverrides = [];
    let automaticParameterRecalculationTimer = 0;
    const scheduledAutomaticParameterChanges = new Map();
    const boundEditableParameterControls = new WeakSet();
    const equipmentParameterDraftStates = new WeakMap();
    let activeEquipmentParameterDraftTable = null;

    function editableParameterControls(table) {
      if (!table) return [];
      return editableTableRows(table)
        .map((tr) => tr.querySelector('input.param-table-input, select.param-table-input'))
        .filter((control) => Boolean(control) && control.dataset.resultEditable !== 'false');
    }

    function equipmentParameterDraftState(table, createIfMissing = true) {
      if (!table) return null;
      let state = equipmentParameterDraftStates.get(table);
      if (!state && createIfMissing) {
        state = {
          editing: false,
          submitting: false,
          snapshot: new Map(),
          toolbar: null,
          editSequence: 0,
        };
        equipmentParameterDraftStates.set(table, state);
      }
      return state || null;
    }

    function clearAutomaticParameterRecalculationTimer() {
      if (!automaticParameterRecalculationTimer) return;
      const clear = typeof window !== 'undefined' && window.clearTimeout
        ? window.clearTimeout.bind(window)
        : (typeof clearTimeout === 'function' ? clearTimeout : null);
      if (clear) clear(automaticParameterRecalculationTimer);
      automaticParameterRecalculationTimer = 0;
    }

    function cancelScheduledAutomaticParameterRecalculation() {
      clearAutomaticParameterRecalculationTimer();
      scheduledAutomaticParameterChanges.clear();
    }

    function equipmentResultStateFromRow(table, tr, control) {
      if (!table || !tr || !control) return null;
      if (control.dataset && control.dataset.resultEditable === 'false') return null;
      const headers = getMarkdownTableHeaders(table);
      const meta = detectEditableResultTable(headers);
      if (!meta || meta.kind !== 'equipment_result') return null;
      const cells = tr.querySelectorAll('td');
      const labelIndex = meta.nameIndex >= 0
        ? meta.nameIndex
        : findExactHeaderIndex(headers, ['参数', '参数名称', '设备名称']);
      const label = labelIndex >= 0
        ? normalizeEditableCellValue((cells[labelIndex] && cells[labelIndex].textContent) || '')
        : '';
      const value = normalizeEditableCellValue(control.value);
      const original = normalizeEditableCellValue(control.dataset.originalValue || '');
      return {
        label,
        value,
        original,
        table,
        tr,
        control,
        editSequence: Number(control.dataset.parameterDraftEditSequence || 0),
      };
    }

    function equipmentParameterDraftChanges(table) {
      if (!table) return [];
      const changes = [];
      editableTableRows(table).forEach((tr) => {
        const control = tr.querySelector('input.param-table-input, select.param-table-input');
        const edit = equipmentResultStateFromRow(table, tr, control);
        if (!edit || !edit.label || !edit.value || edit.value === edit.original) return;
        changes.push(edit);
      });
      return changes.sort((left, right) => Number(left.editSequence || 0) - Number(right.editSequence || 0));
    }

    function setEquipmentParameterControlsEnabled(table, enabled) {
      editableParameterControls(table).forEach((control) => {
        control.disabled = !enabled;
      });
    }

    function updateEquipmentParameterDraftToolbar(table) {
      const state = equipmentParameterDraftState(table, false);
      if (!state) return;
      const changes = equipmentParameterDraftChanges(table);
      const changedCount = changes.length;
      if (table.dataset) table.dataset.parameterDraftCount = String(changedCount);
      if (table.classList && typeof table.classList.toggle === 'function') {
        table.classList.toggle('parameter-draft-editing', state.editing);
        table.classList.toggle('parameter-draft-submitting', state.submitting);
      }
      setEquipmentParameterControlsEnabled(table, state.editing && !state.submitting);
      const toolbar = state.toolbar;
      if (!toolbar) return;
      toolbar.editButton.hidden = state.editing || state.submitting;
      toolbar.confirmButton.hidden = !state.editing;
      toolbar.cancelButton.hidden = !state.editing;
      toolbar.confirmButton.disabled = state.submitting || changedCount === 0;
      toolbar.cancelButton.disabled = state.submitting;
      if (state.submitting) {
        toolbar.status.textContent = `已提交 ${changedCount} 项修改，正在重新计算...`;
      } else if (state.editing) {
        toolbar.status.textContent = changedCount
          ? `已暂存 ${changedCount} 项修改，尚未提交计算`
          : '编辑模式：可连续修改多个参数';
      } else {
        toolbar.status.textContent = '点击“编辑参数”后修改，确认时统一计算';
      }
    }

    function cancelEquipmentParameterDraft(table, options = {}) {
      const state = equipmentParameterDraftState(table, false);
      if (!state) return false;
      if (options.restore !== false) {
        state.snapshot.forEach((value, control) => {
          if (control) control.value = value;
        });
      }
      state.editing = false;
      state.submitting = false;
      state.snapshot.clear();
      state.editSequence = 0;
      editableParameterControls(table).forEach((control) => {
        if (control && control.dataset) delete control.dataset.parameterDraftEditSequence;
      });
      if (activeEquipmentParameterDraftTable === table) activeEquipmentParameterDraftTable = null;
      updateEquipmentParameterDraftToolbar(table);
      syncEditableParameterTableFromDom(table);
      if (!options.silent && typeof setStatus === 'function') {
        setStatus('已取消本次参数草稿，未执行计算。');
      }
      return true;
    }

    function beginEquipmentParameterDraft(table) {
      const state = equipmentParameterDraftState(table);
      if (!state || state.submitting) return false;
      if (activeEquipmentParameterDraftTable && activeEquipmentParameterDraftTable !== table) {
        cancelEquipmentParameterDraft(activeEquipmentParameterDraftTable, { silent: true });
      }
      state.snapshot.clear();
      state.editSequence = 0;
      editableParameterControls(table).forEach((control) => {
        state.snapshot.set(control, normalizeEditableCellValue(control.value));
        if (control.dataset) delete control.dataset.parameterDraftEditSequence;
      });
      state.editing = true;
      state.submitting = false;
      activeEquipmentParameterDraftTable = table;
      updateEquipmentParameterDraftToolbar(table);
      const firstControl = editableParameterControls(table)[0];
      if (firstControl && typeof firstControl.focus === 'function') firstControl.focus();
      if (typeof setStatus === 'function') {
        setStatus('已进入参数编辑模式。可连续修改，完成后点击“确认修改并计算”。');
      }
      return true;
    }

    function confirmEquipmentParameterDraft(table) {
      const state = equipmentParameterDraftState(table, false);
      if (!state || !state.editing || state.submitting) return false;
      const changes = equipmentParameterDraftChanges(table);
      if (!changes.length) {
        cancelEquipmentParameterDraft(table, { restore: false, silent: true });
        if (typeof setStatus === 'function') setStatus('没有检测到参数变化。');
        return false;
      }
      state.editing = false;
      state.submitting = true;
      if (activeEquipmentParameterDraftTable === table) activeEquipmentParameterDraftTable = null;
      updateEquipmentParameterDraftToolbar(table);
      const accepted = typeof requestAutomaticParameterRecalculation === 'function'
        && requestAutomaticParameterRecalculation({
          trigger: changes[changes.length - 1],
          changes,
          confirmed_draft: true,
        });
      if (!accepted) {
        state.editing = true;
        state.submitting = false;
        activeEquipmentParameterDraftTable = table;
        updateEquipmentParameterDraftToolbar(table);
        if (typeof setStatus === 'function') setStatus('提交参数草稿失败，请稍后重试。', true);
        return false;
      }
      return true;
    }

    function ensureEquipmentParameterDraftToolbar(table) {
      const state = equipmentParameterDraftState(table);
      if (!state || state.toolbar || typeof document === 'undefined' || !document.createElement) {
        updateEquipmentParameterDraftToolbar(table);
        return state;
      }
      const legend = document.createElement('div');
      legend.className = 'parameter-draft-legend';
      legend.innerHTML = [
        '<span class="parameter-draft-legend-item"><span class="parameter-draft-legend-swatch editable"></span>绿色：可修改</span>',
        '<span class="parameter-draft-legend-item"><span class="parameter-draft-legend-swatch readonly"></span>灰色：不可修改（自动计算）</span>'
      ].join('');
      const root = document.createElement('div');
      root.className = 'parameter-draft-toolbar';
      const status = document.createElement('span');
      status.className = 'parameter-draft-status';
      const actions = document.createElement('div');
      actions.className = 'parameter-draft-actions';
      const editButton = document.createElement('button');
      editButton.type = 'button';
      editButton.className = 'parameter-draft-button parameter-draft-edit';
      editButton.textContent = '编辑参数';
      const confirmButton = document.createElement('button');
      confirmButton.type = 'button';
      confirmButton.className = 'parameter-draft-button parameter-draft-confirm';
      confirmButton.textContent = '确认修改并计算';
      const cancelButton = document.createElement('button');
      cancelButton.type = 'button';
      cancelButton.className = 'parameter-draft-button parameter-draft-cancel';
      cancelButton.textContent = '取消';
      editButton.addEventListener('click', () => beginEquipmentParameterDraft(table));
      confirmButton.addEventListener('click', () => confirmEquipmentParameterDraft(table));
      cancelButton.addEventListener('click', () => cancelEquipmentParameterDraft(table));
      actions.appendChild(editButton);
      actions.appendChild(confirmButton);
      actions.appendChild(cancelButton);
      root.appendChild(status);
      root.appendChild(actions);
      state.toolbar = { root, status, actions, editButton, confirmButton, cancelButton, legend };
      if (table.parentNode && typeof table.parentNode.insertBefore === 'function') {
        table.parentNode.insertBefore(legend, table);
        table.parentNode.insertBefore(root, table);
      }
      updateEquipmentParameterDraftToolbar(table);
      return state;
    }

    function scheduleAutomaticParameterRecalculation(table, tr, control) {
      const edit = equipmentResultStateFromRow(table, tr, control);
      if (!edit) return false;
      const state = equipmentParameterDraftState(table, false);
      if (!state || !state.editing || state.submitting) return true;
      state.editSequence += 1;
      if (control.dataset) control.dataset.parameterDraftEditSequence = String(state.editSequence);
      updateEquipmentParameterDraftToolbar(table);
      return true;
    }

    function markAutomaticParameterRecalculationApplied(changes) {
      const affectedTables = new Set();
      (Array.isArray(changes) ? changes : []).forEach((item) => {
        const control = item && item.control;
        if (!control || !control.dataset) return;
        const submitted = normalizeEditableCellValue(item.value);
        const current = normalizeEditableCellValue(control.value);
        if (submitted && current === submitted) control.dataset.originalValue = submitted;
        if (item.table) affectedTables.add(item.table);
      });
      affectedTables.forEach((table) => {
        const state = equipmentParameterDraftState(table, false);
        if (!state) return;
        state.editing = false;
        state.submitting = false;
        state.snapshot.clear();
        state.editSequence = 0;
        updateEquipmentParameterDraftToolbar(table);
        syncEditableParameterTableFromDom(table);
      });
    }

    function bindEditableParameterControl(table, tr, control) {
      if (!table || !tr || !control || boundEditableParameterControls.has(control)) return;
      const handleUserEdit = (event) => {
        const handledAsDraft = scheduleAutomaticParameterRecalculation(table, tr, control, event && event.type);
        if (!handledAsDraft) syncEditableParameterTableFromDom(table);
      };
      control.addEventListener('change', handleUserEdit);
      control.addEventListener('input', handleUserEdit);
      boundEditableParameterControls.add(control);
    }

    const CANONICAL_AUTOFILL_NAME_ALIASES = {
      // Formula/extraction keys.
      boiler_capacity: '机组容量',
      max_slag_rate: '锅炉最大排渣量',
      boiler_normal_slag_rate: '锅炉正常排渣量',
      dry_slag_bulk_density: '干渣堆积密度',
      slag_well_max_storage_time: '渣井最大储渣时间',
      slag_well_count: '渣井口数',
      n: '渣井口数',
      comb_plate_length: '锅炉梳型密封板长度',
      comb_plate_width: '锅炉梳型密封板宽度',
      boiler_down_expansion: '锅炉下膨胀量',
      t1: '下联箱标高',
      d1: '渣仓直径',
      cv1: '渣仓有效容积',
      ct1: '渣仓顶标高',
      cone_included_angle: '锥段角度',
      repose_angle: '灰渣安息角',
      silo_top_cut: '干渣仓仓顶是否切角',

      // Template parameter names and legacy calculation result labels.
      '锅炉下联箱标高': '下联箱标高',
      '锅炉下联箱标高t1': '下联箱标高',
      '下联箱标高': '下联箱标高',
      '下联箱梳形板长度': '锅炉梳型密封板长度',
      '锅炉梳型密封板长度': '锅炉梳型密封板长度',
      '关断门箱体长度': '锅炉梳型密封板长度',
      '下联箱梳形板宽度': '锅炉梳型密封板宽度',
      '锅炉梳型密封板宽度': '锅炉梳型密封板宽度',
      '关断门箱体宽度': '锅炉梳型密封板宽度',
      'l密封板自身高度': 'L密封板自身高度',
      '关断门箱体高度': 'L密封板自身高度',
      '锅炉梳型密封板连接螺栓高度差': '锅炉梳型密封板连接螺栓高度差',
      'l密封板与密封梁角钢水平距离': 'L密封板与密封梁角钢水平距离',
      'l密封板与密封梁角钢高度差': 'L密封板与密封梁角钢高度差',
      '密封形式': '密封形式',
      '渣井口数': '渣井口数',
      '关断门口数': '渣井口数',
      '渣井口间距': '渣井口间距',
      '关断门口间距': '渣井口间距',
      '横向立柱间距是否相同': '横向立柱间距是否相同',
      '渣井横向立柱间距': '渣井横向立柱间距',
      '渣井纵向立柱间距': '渣井纵向立柱间距',
      '渣井壁板上口高度': '渣井壁板上口高度',
      '渣井壁板上口标高': '渣井壁板上口高度',
      '渣井壁板下口高度': '渣井壁板下口高度',
      '渣井下口标高': '渣井壁板下口高度',
      '渣井平台高度': '渣井平台高度',
      '渣井中间壁板顶端高度': '渣井中间壁板顶端高度',
      '渣井中间壁板顶端间距': '渣井中间壁板顶端间距',
      '渣井壁板上口长度': '渣井壁板上口长度',
      '渣井壁板上口宽度': '渣井壁板上口宽度',
      '渣井偏心距离': '渣井偏心距离',
      '渣井左右端壁板角度': '渣井左右端壁板角度',
      '渣井左右壁板角度': '渣井左右端壁板角度',
      '渣井壁板角度': '渣井左右端壁板角度',
      '渣井倾角壳体倾角': '渣井左右端壁板角度',
      '渣井前端壁板角度': '渣井前端壁板角度',
      '渣井后端壁板角度': '渣井后端壁板角度',
      '渣井中间端壁板角度': '渣井中间端壁板角度',
      '是否有渣井平台': '是否有渣井平台',
      '渣井平台左右端宽度': '渣井平台左右端宽度',
      '渣井平台前端宽度': '渣井平台前端宽度',
      '渣井平台后端宽度': '渣井平台后端宽度',
      '渣井斜梯类型': '渣井斜梯类型',
      '渣井斜梯定位类型': '渣井斜梯定位类型',
      '渣井斜梯至锅炉中心面距离': '渣井斜梯至锅炉中心面距离',

      // Common dry silo names.
      '干渣仓直径d1': '渣仓直径',
      '干渣仓直径': '渣仓直径',
      '渣仓直径': '渣仓直径',
      '仓体直径': '渣仓直径',
      '筒体直径': '渣仓直径',
      '混合直径': '渣仓直径',
      '干渣仓有效容积cv1': '渣仓有效容积',
      '干渣仓有效容积': '渣仓有效容积',
      '渣仓有效容积': '渣仓有效容积',
      '有效容积': '渣仓有效容积',
      '贮渣容积': '渣仓有效容积',
      '渣仓顶标高': '渣仓顶标高',
      '渣仓仓顶标高': '渣仓顶标高',
      '干渣仓锥体顶标高ct1': '渣仓顶标高',
      '干渣仓锥体顶标高': '渣仓顶标高',
      '锥体顶标高': '渣仓顶标高',
      '顶标高': '渣仓顶标高',
      '干渣仓仓顶是否切角': '干渣仓仓顶是否切角',
      '干渣仓仓顶是否有切角': '干渣仓仓顶是否切角',
      '渣仓仓体仓顶是否切角': '干渣仓仓顶是否切角',
      '渣仓仓体仓顶是否有切角': '干渣仓仓顶是否切角'
    };

    function canonicalAutofillDisplayName(name) {
      const raw = stripMarkdown(name)
        .replace(/\[[^\]]*\]/g, '')
        .replace(/【[^】]*】/g, '')
        .replace(/[（(][^）)]*[）)]/g, '')
        .trim();
      if (!raw) return '';
      const normalized = normalizeParamKey(raw);
      const direct = CANONICAL_AUTOFILL_NAME_ALIASES[normalized];
      if (direct) return direct;
      return raw.replace(/\s+/g, '');
    }

    function canonicalAutofillKey(name) {
      return normalizeParamKey(canonicalAutofillDisplayName(name));
    }

    function autofillSourceRank(source) {
      const text = String(source || '');
      if (/已编辑/.test(text)) return 130;
      if (/设备选型结果|当前响应|cidu_selection_meta/.test(text)) return 110;
      if (/输入参数核对|参数提取结果|selection\.parameters|sessionStorage\.parameters/.test(text)) return 80;
      if (/numeric|text/.test(text)) return 60;
      if (/markdown|候选池/.test(text)) return 40;
      return 50;
    }

    function setPendingEquipmentResultOverrides(overrides) {
      pendingEquipmentResultOverrides = Array.isArray(overrides)
        ? overrides.filter((item) => item && item.label && item.value)
        : [];
    }

    function setPendingParameterResultOverrides(overrides) {
      pendingParameterResultOverrides = Array.isArray(overrides)
        ? overrides.filter((item) => item && item.key && item.value)
        : [];
    }

    function markdownTableCell(value) {
      return String(value == null ? '' : value).replace(/\|/g, '\\|');
    }

    function applyEquipmentOverridesToRows(rows, overrides) {
      if (!Array.isArray(rows) || !rows.length || !Array.isArray(overrides) || !overrides.length) return rows;
      const byLabel = new Map();
      overrides.forEach((item) => byLabel.set(normalizeParamKey(item.label), String(item.value)));
      return rows.map((row) => {
        const label = rowValueByHeader(row, ['参数', '设备名称', '参数名称']);
        const key = normalizeParamKey(label);
        if (!key || !byLabel.has(key)) return row;
        const next = Object.assign({}, row);
        const valueHeaders = ['参数值', '设备型号/参数', '型号/参数', '使用值', '数值', '值'];
        const targetHeader = valueHeaders.find((header) => Object.prototype.hasOwnProperty.call(next, header)) || '参数值';
        next[targetHeader] = byLabel.get(key);
        return next;
      });
    }

    function applyPendingEquipmentResultOverridesToMarkdown(markdown) {
      const overrides = pendingEquipmentResultOverrides || [];
      if (!overrides.length || !markdown) return markdown;
      const byLabel = new Map();
      overrides.forEach((item) => byLabel.set(normalizeParamKey(item.label), String(item.value)));
      const lines = String(markdown).replace(/\r\n/g, '\n').split('\n');
      for (let index = 0; index < lines.length - 2; index += 1) {
        const headers = splitMarkdownTableLine(lines[index]);
        if (!headers.length || !isMarkdownTableSeparator(lines[index + 1])) continue;
        const nameIndex = findHeaderIndex(headers, ['参数', '设备名称', '参数名称']);
        const valueIndex = findHeaderIndex(headers, ['参数值', '设备型号/参数', '型号/参数', '使用值', '数值', '值']);
        if (nameIndex < 0 || valueIndex < 0) continue;
        let rowIndex = index + 2;
        while (rowIndex < lines.length) {
          const cells = splitMarkdownTableLine(lines[rowIndex]);
          if (!cells.length) break;
          const key = normalizeParamKey(cells[nameIndex]);
          if (key && byLabel.has(key)) {
            while (cells.length < headers.length) cells.push('');
            cells[valueIndex] = byLabel.get(key);
            lines[rowIndex] = '| ' + cells.map(markdownTableCell).join(' | ') + ' |';
          }
          rowIndex += 1;
        }
        index = rowIndex;
      }
      return lines.join('\n');
    }

    function applyPendingParameterResultOverridesToMarkdown(markdown) {
      const overrides = pendingParameterResultOverrides || [];
      if (!overrides.length || !markdown) return markdown;
      const byKey = new Map();
      overrides.forEach((item) => {
        byKey.set(normalizeParamKey(item.key), String(item.value));
        if (item.name) byKey.set(normalizeParamKey(item.name), String(item.value));
      });
      const lines = String(markdown).replace(/\r\n/g, '\n').split('\n');
      for (let index = 0; index < lines.length - 2; index += 1) {
        const headers = splitMarkdownTableLine(lines[index]);
        if (!headers.length || !isMarkdownTableSeparator(lines[index + 1])) continue;
        const meta = detectEditableResultTable(headers);
        if (!meta || meta.kind !== 'param_extract' || meta.nameIndex < 0 || meta.valueIndex < 0) continue;
        let rowIndex = index + 2;
        while (rowIndex < lines.length) {
          const cells = splitMarkdownTableLine(lines[rowIndex]);
          if (!cells.length) break;
          const key = normalizeParamKey(cells[meta.nameIndex]);
          if (key && byKey.has(key)) {
            while (cells.length < headers.length) cells.push('');
            cells[meta.valueIndex] = byKey.get(key);
            lines[rowIndex] = '| ' + cells.map(markdownTableCell).join(' | ') + ' |';
          }
          rowIndex += 1;
        }
        index = rowIndex;
      }
      return lines.join('\n');
    }

    function applyPendingEquipmentResultOverridesToPayload(payload) {
      // 结果值必须来自工作流公式节点的真实重算。旧逻辑会在响应到达后直接改 Markdown，
      // 导致当前行看似修改成功、依赖它的二次计算仍是旧值。现在只清理待提交状态，
      // 不再篡改服务端返回内容或最新计算快照。
      setPendingEquipmentResultOverrides([]);
      setPendingParameterResultOverrides([]);
      return payload || {};
    }

    function collectAutofillCandidatesFromRows(rows, candidates, options = {}) {
      const nameScoreBias = Number(options.nameScoreBias != null ? options.nameScoreBias : 4);
      const idScoreBias = Number(options.idScoreBias != null ? options.idScoreBias : 1);
      const deviceScoreBias = Number(options.deviceScoreBias != null ? options.deviceScoreBias : 8);
      const sourceLabel = options.sourceLabel || '参数提取结果';
      (rows || []).forEach((row) => {
        const tableParamLabel = rowValueByHeader(row, ['参数', '设备名称']);
        const tableParamValue = rowValueByHeader(row, ['参数值', '设备型号/参数', '型号/参数']);
        if (tableParamLabel && tableParamValue) {
          const normalizedName = typeof normalizeEquipmentParamName === 'function'
            ? normalizeEquipmentParamName(tableParamLabel)
            : tableParamLabel;
          addAutofillCandidate(candidates, normalizedName, tableParamValue, {
            source: sourceLabel,
            kind: inferAutofillCandidateKind(normalizedName, tableParamValue, 'param_value'),
            scoreBias: deviceScoreBias
          });
        }

        const paramName = rowValueByHeader(row, ['参数中文名称', '参数名称', '参数名']);
        const paramId = rowValueByHeader(row, ['参数标识', '参数id', '参数ID']);
        const paramValue = rowValueByHeader(row, AUTOFILL_VALUE_HEADERS);
        const unit = rowValueByHeader(row, ['单位']);
        if (paramName && paramValue) {
          addAutofillCandidate(candidates, paramName, paramValue, {
            unit,
            source: sourceLabel,
            kind: 'param_value',
            scoreBias: nameScoreBias
          });
        }
        if (paramId && paramValue) {
          addAutofillCandidate(candidates, paramId, paramValue, {
            unit,
            source: sourceLabel,
            kind: 'param_value',
            scoreBias: idScoreBias
          });
        }

        // 兜底：部分工作流会把设备选型/参数提取输出成非标准表头。
        // 只要某一行里能找到“像参数名”的列和“像参数值”的列，就加入候选池。
        const entries = Object.entries(row || {}).filter(([key, value]) => {
          return key && !isIgnoredParameterName(key) && isValidAutofillValue(value);
        });
        const valueEntry = entries.find(([key, value]) => {
          const k = normalizeParamKey(key);
          return ['设备型号参数', '型号参数', '参数值', '使用值', '数值', '值', '当前值'].some((name) => k.includes(normalizeParamKey(name)))
            && isValidAutofillValue(value);
        });
        const nameEntry = entries.find(([key, value]) => {
          if (!valueEntry || key === valueEntry[0]) return false;
          const k = normalizeParamKey(key);
          return ['设备名称', '参数名称', '参数中文名称', '名称', '参数名'].some((name) => k.includes(normalizeParamKey(name)))
            && isValidAutofillValue(value);
        });
        if (nameEntry && valueEntry) {
          const fallbackNameHeader = normalizeParamKey(nameEntry[0]);
          const fallbackValueHeader = normalizeParamKey(valueEntry[0]);
          const fallbackKind = fallbackNameHeader.includes('设备名称') || fallbackValueHeader.includes('设备型号')
            ? inferAutofillCandidateKind(nameEntry[1], valueEntry[1], 'device_model')
            : 'param_value';
          addAutofillCandidate(candidates, nameEntry[1], valueEntry[1], {
            unit,
            source: '表格兜底解析',
            kind: fallbackKind,
            scoreBias: 3
          });
        }
      });
    }

    function collectAutofillCandidatesFromMarkdownTable(text, candidates) {
      const lines = String(text || '').replace(/\r\n/g, '\n').split('\n');
      for (let index = 0; index < lines.length - 2; index += 1) {
        const headers = splitMarkdownTableLine(lines[index]);
        if (!headers.length || !isMarkdownTableSeparator(lines[index + 1])) continue;

        const deviceNameIndex = findHeaderIndex(headers, ['参数', '设备名称']);
        const deviceValueIndex = findHeaderIndex(headers, ['参数值', '设备型号/参数', '型号/参数']);
        const paramIdIndex = findHeaderIndex(headers, ['参数标识', '参数id', '参数ID']);
        const paramNameIndex = findHeaderIndex(headers, ['参数中文名称', '参数名称', '参数名']);
        const paramValueIndex = findHeaderIndex(headers, AUTOFILL_VALUE_HEADERS);
        const unitIndex = findHeaderIndex(headers, ['单位']);
        const useDeviceTable = deviceNameIndex >= 0 && deviceValueIndex >= 0;
        const useParamTable = paramValueIndex >= 0 && (paramNameIndex >= 0 || paramIdIndex >= 0);
        if (!useDeviceTable && !useParamTable) continue;

        let rowIndex = index + 2;
        while (rowIndex < lines.length) {
          const cells = splitMarkdownTableLine(lines[rowIndex]);
          if (!cells.length) break;
          if (useDeviceTable) {
            const label = typeof normalizeEquipmentParamName === 'function'
              ? normalizeEquipmentParamName(cells[deviceNameIndex])
              : cells[deviceNameIndex];
            addAutofillCandidate(candidates, label, cells[deviceValueIndex], {
              source: '设备选型结果',
              kind: inferAutofillCandidateKind(label, cells[deviceValueIndex], 'param_value'),
              scoreBias: 8
            });
          }
          if (useParamTable) {
            const unit = unitIndex >= 0 ? cells[unitIndex] : '';
            if (paramNameIndex >= 0) {
              addAutofillCandidate(candidates, cells[paramNameIndex], cells[paramValueIndex], {
                unit,
                source: '参数提取结果',
                kind: 'param_value',
                scoreBias: 4
              });
            }
            if (paramIdIndex >= 0) {
              addAutofillCandidate(candidates, cells[paramIdIndex], cells[paramValueIndex], {
                unit,
                source: '参数提取结果',
                kind: 'param_value',
                scoreBias: 1
              });
            }
          }
          rowIndex += 1;
        }
        index = rowIndex;
      }
    }

    function collectAutofillCandidatesFromText(text, candidates) {
      const raw = String(text || '');
      if (!raw.trim()) return;
      collectAutofillCandidatesFromMarkdownTable(raw, candidates);
      collectAutofillCandidatesFromRows(parseParameterRows(raw), candidates);
      Object.entries(parseMarkdownParameters(raw)).forEach(([key, value]) => {
        addAutofillCandidate(candidates, key, value, { source: 'markdown', kind: 'param_value', scoreBias: 1 });
      });
    }

    function collectAutofillCandidatesFromResult(result, candidates) {
      if (!result) return;
      Object.entries(result.numeric || {}).forEach(([key, value]) => {
        addAutofillCandidate(candidates, key, value, { source: 'numeric', kind: 'param_value', scoreBias: 2 });
      });
      Object.entries(result.text || {}).forEach(([key, value]) => {
        addAutofillCandidate(candidates, key, value, { source: 'text', kind: 'param_value', scoreBias: 1 });
      });
      collectAutofillCandidatesFromText(result.answer_markdown || result.message || '', candidates);
    }

    function collectAutofillCandidates(result) {
      syncAllEditableParameterTablesToSelection();
      const candidates = [];
      const conversation = activeConversation();
      const messages = conversation && Array.isArray(conversation.messages) ? conversation.messages : [];
      messages.forEach((message) => {
        if (!message) return;
        if (message.payload) collectAutofillCandidatesFromResult(message.payload, candidates);
        collectAutofillCandidatesFromText(messageTextForParameterScan(message), candidates);
      });
      collectAutofillCandidatesFromResult(result, candidates);
      // 用户在「输入参数核对」等表格中的最新编辑优先于对话历史 Markdown 里的旧值。
      if (currentSelection) {
        if (Array.isArray(currentSelection.parameter_rows) && currentSelection.parameter_rows.length) {
          collectAutofillCandidatesFromRows(currentSelection.parameter_rows, candidates, {
            sourceLabel: '输入参数核对',
            nameScoreBias: 25,
            idScoreBias: 22
          });
        }
        if (Array.isArray(currentSelection.equipment_result_rows) && currentSelection.equipment_result_rows.length) {
          collectAutofillCandidatesFromRows(currentSelection.equipment_result_rows, candidates, {
            sourceLabel: '设备选型结果(已编辑)',
            nameScoreBias: 20,
            idScoreBias: 18,
            deviceScoreBias: 20
          });
        }
      }
      return dedupeAutofillCandidates(candidates);
    }

    function autofillCandidateQuality(candidate) {
      if (!candidate) return 0;
      let score = Number(candidate.sourceRank || 0) + Number(candidate.scoreBias || 0);
      if (candidate.sourceUnit) score += 5;
      if (candidate.canonicalKey) score += 4;
      if (candidate.kind === 'param_value') score += 2;
      return score;
    }

    function dedupeAutofillCandidates(candidates) {
      const byCanonical = new Map();
      const loose = [];
      (candidates || []).forEach((candidate) => {
        const key = candidate && candidate.canonicalKey;
        if (!key) {
          loose.push(candidate);
          return;
        }
        const prev = byCanonical.get(key);
        if (!prev || autofillCandidateQuality(candidate) >= autofillCandidateQuality(prev)) {
          byCanonical.set(key, candidate);
        }
      });
      return [...byCanonical.values(), ...loose.filter(Boolean)];
    }

    const AUTOFILL_CONTEXT_LABELS = [
      '渣仓', '干渣仓', '斜梯', '爬梯', '扶梯', '梯子', '梯',
      '平台', '仓顶平台', '干渣机', '渣机', '渣井', '锅炉'
    ];

    const AUTOFILL_TOKEN_DICTIONARY = [
      '斜梯', '爬梯', '扶梯', '平台', '仓顶平台', '渣仓', '干渣仓', '干渣机', '渣机', '渣井', '锅炉',
      '设备型号', '规格型号', '型号', '规格', '物料编码', '编码', '前缀',
      '内宽', '宽度', '角度', '夹角', '倾角', '锥角', '高度', '长度', '间距', '中心',
      '直径', '内径', '外径', '半径', '容积', '有效容积', '顶标高', '底标高', '标高',
      '锥体', '锥斗', '底锥', '柱段', '梯踏板', '踏板', '纵条', '底板', '类型',
      '连接', '角钢', '槽钢', '圆钢', '钢板', '支撑', '支架', '法兰',
      '数量', '个数', '数目', '口数', '根数', '件数',
      '厚度', '材质', '重量', '质量', '容量', '流量', '温度', '压力', '速度',
      '延伸量', '位移', '间隙'
    ];

    const AUTOFILL_NUMERIC_TARGET_TOKENS = [
      '数量', '个数', '数目', '口数', '根数', '件数',
      '内宽', '宽度', '角度', '夹角', '倾角', '锥角', '高度', '长度', '间距',
      '直径', '内径', '外径', '半径', '容积', '有效容积', '顶标高', '底标高', '标高',
      '厚度', '重量', '质量', '容量', '流量', '温度', '压力', '速度',
      '延伸量', '位移', '间隙'
    ];

    function compactAutofillName(name) {
      return normalizeParamKey(name)
        .replace(/^干(?=渣仓|渣机|渣井)/, '')
        .replace(/^(?:设备|模型|参数)/, '');
    }

    function comparableAutofillName(name) {
      return compactAutofillName(name).replace(/[×*]/g, 'x');
    }

    function stripEquipmentPrefixForAutofill(name) {
      let text = compactAutofillName(name);
      const prefixes = ['干渣仓', '渣仓', '干渣机', '渣机', '干渣井', '渣井', '锅炉', '斜梯', '爬梯', '扶梯', '平台', '仓顶平台'];
      prefixes.map(compactAutofillName).sort((a, b) => b.length - a.length).some((prefix) => {
        if (prefix && text.startsWith(prefix) && text.length > prefix.length + 1) {
          text = text.slice(prefix.length);
          return true;
        }
        return false;
      });
      return text;
    }

    function autofillContextTokens(name) {
      const text = compactAutofillName(name);
      return AUTOFILL_CONTEXT_LABELS
        .map(compactAutofillName)
        .filter((token) => token && text.includes(token));
    }

    function autofillSpecificContextTokens(name) {
      const generic = new Set(['梯']);
      return autofillContextTokens(name).filter((token) => !generic.has(token));
    }

    function autofillTokens(name) {
      const normalized = compactAutofillName(name);
      if (!normalized) return [];
      const rawParts = String(name || '')
        .replace(/[【】\[\]（）(),，:：;；=]/g, '-')
        .split(/[-_\s/\\]+/)
        .map(compactAutofillName)
        .filter(Boolean);
      const dictionary = AUTOFILL_TOKEN_DICTIONARY.map(compactAutofillName);
      const out = new Set(rawParts);
      dictionary.forEach((token) => {
        if (token && normalized.includes(token)) out.add(token);
      });
      return [...out].filter((token) => token.length >= 1);
    }

    function autofillParameterTokens(name) {
      const context = new Set(autofillContextTokens(name));
      return autofillTokens(name).filter((token) => {
        if (!token || context.has(token)) return false;
        return token.length >= 2;
      });
    }

    function autofillSemanticTokens(name) {
      const text = compactAutofillName(name);
      const contexts = new Set(autofillContextTokens(name));
      const tokens = new Set();
      AUTOFILL_TOKEN_DICTIONARY
        .map(compactAutofillName)
        .filter(Boolean)
        .sort((a, b) => b.length - a.length)
        .forEach((token) => {
          if (!contexts.has(token) && text.includes(token)) tokens.add(token);
        });
      const asciiTokens = String(name || '').match(/[a-zA-Z]+[a-zA-Z0-9_]*/g) || [];
      asciiTokens.forEach((token) => {
        const normalized = comparableAutofillName(token);
        if (normalized && !['mm', 'cm', 'm', 'm3'].includes(normalized)) tokens.add(normalized);
      });
      return [...tokens];
    }

    function isAutofillContextOnlyName(name) {
      const text = compactAutofillName(name);
      if (!text) return false;
      return autofillContextTokens(name).some((token) => token === text);
    }

    function inferAutofillCandidateKind(key, value, fallback = 'param_value') {
      const name = compactAutofillName(key);
      const split = splitAutofillValueAndUnit(value);
      if (split.numeric != null && !isNaN(split.numeric)) return 'param_value';
      const indicators = AUTOFILL_TOKEN_DICTIONARY
        .filter((token) => !AUTOFILL_CONTEXT_LABELS.includes(token))
        .map(compactAutofillName);
      if (indicators.some((token) => token && name.includes(token))) return 'param_value';
      return fallback || 'param_value';
    }

    function autofillAliasScore(targetName, sourceName) {
      const target = comparableAutofillName(targetName);
      const source = comparableAutofillName(sourceName);
      if (!target || !source) return 0;
      const groups = [
        ['渣仓直径', '混合直径', '干渣仓直径', '仓体直径', '筒体直径', 'd1'],
        ['渣仓有效容积', '干渣仓有效容积', '有效容积', '贮渣容积', 'cv1'],
        ['锥体顶标高', '锥段顶标高', '干渣仓锥体顶标高', 'ct1'],
        ['锥斗夹角', '底锥夹角', '锥角', 'coneincludedangle'],
        ['灰渣安息角', '安息角', 'reposeangle'],
        ['斜梯内宽', '梯子内宽', '内宽', '宽度'],
        ['斜梯角度', '梯子角度', '角度', '倾角'],
        ['斜梯高度', '梯子高度', '高度'],
        ['安装间隙', '间隙', 'installclearance'],
        ['固定端水平延伸量', '水平延伸量', '延伸量', 'fixedhorizontalextension'],
        ['热位移量', '热位移', '位移量', 'thermaldisplacement'],
        ['连接角钢数量', '与渣仓连接角钢数量', '角钢数量']
      ];
      return groups.some((group) => {
        const normalized = group.map(comparableAutofillName).filter(Boolean);
        return normalized.some((item) => target.includes(item)) && normalized.some((item) => source.includes(item));
      }) ? 94 : 0;
    }

    function autofillRequiredNameGroups(targetName) {
      const target = comparableAutofillName(targetName);
      const specs = [
        { when: ['角钢'], any: ['角钢'] },
        { when: ['连接'], any: ['连接', '角钢'] },
        { when: ['数量', '个数', '数目'], any: ['数量', '个数', '数目', '口数', '根数', '件数'] },
        { when: ['直径', '内径', '外径'], any: ['直径', '混合直径', '内径', '外径', '仓体直径', '筒体直径', 'd1'] },
        { when: ['内宽', '宽度'], any: ['内宽', '宽度'] },
        { when: ['角度', '夹角', '倾角', '锥角'], any: ['角度', '夹角', '倾角', '锥角', 'coneincludedangle'] },
        { when: ['有效容积', '容积'], any: ['有效容积', '容积', 'cv1'] },
        { when: ['顶标高', '底标高', '标高'], any: ['顶标高', '底标高', '标高', 'ct1'] },
        { when: ['高度'], any: ['高度'] },
        { when: ['长度'], any: ['长度'] },
        { when: ['间距'], any: ['间距'] },
        { when: ['厚度'], any: ['厚度'] },
        { when: ['物料编码', '编码', '前缀'], any: ['物料编码', '编码', '前缀'] }
      ];
      return specs
        .filter((spec) => spec.when.some((token) => target.includes(comparableAutofillName(token))))
        .map((spec) => spec.any.map(comparableAutofillName).filter(Boolean));
    }

    function hasAutofillRequiredNameCoverage(targetName, sourceName) {
      const source = comparableAutofillName(sourceName);
      const groups = autofillRequiredNameGroups(targetName);
      return groups.every((group) => group.some((token) => source.includes(token)));
    }

    function hasConflictingAutofillContext(targetName, sourceName) {
      const targetContexts = autofillSpecificContextTokens(targetName);
      const sourceContexts = autofillSpecificContextTokens(sourceName);
      if (!targetContexts.length || !sourceContexts.length) return false;
      return !targetContexts.some((token) => sourceContexts.includes(token));
    }

    function targetShouldUseNumericAutofillValue(targetName) {
      const target = comparableAutofillName(targetName);
      if (!target) return false;
      if (['是否', '类型', '型号', '规格', '编码', '前缀', '材质'].some((token) => target.includes(comparableAutofillName(token)))) {
        return false;
      }
      return AUTOFILL_NUMERIC_TARGET_TOKENS
        .map(comparableAutofillName)
        .some((token) => token && target.includes(token));
    }

    function isDeviceModelCandidateAllowed(targetName, sourceName) {
      const target = comparableAutofillName(targetName);
      const source = comparableAutofillName(sourceName);
      if (!target || !source) return false;
      if (target === source) return true;
      if (target.includes(source) && ['型号', '规格', '设备名称', '模型名称', '名称'].some((token) => target.includes(comparableAutofillName(token)))) {
        return true;
      }
      return ['设备型号', '规格型号', '型号', '模型型号', '设备名称'].some((token) => target.includes(comparableAutofillName(token)));
    }

    function isAutofillCandidateAllowedForParam(param, candidate, contextText) {
      const targetName = param && param.name;
      const sourceName = candidate && candidate.key;
      if (!targetName || !sourceName) return false;
      const aliasScore = autofillAliasScore(targetName, sourceName);
      if (hasConflictingAutofillContext(targetName, sourceName) && aliasScore < 90) return false;

      if (candidate.kind === 'device_model') {
        return isDeviceModelCandidateAllowed(targetName, sourceName);
      }

      if (!hasAutofillRequiredNameCoverage(targetName, sourceName) && aliasScore < 90) return false;

      const targetSemanticTokens = autofillSemanticTokens(targetName);
      const sourceSemanticTokens = autofillSemanticTokens(sourceName);
      if (targetSemanticTokens.length && !sourceSemanticTokens.length && aliasScore < 90) return false;
      if (targetSemanticTokens.length && sourceSemanticTokens.length
          && !targetSemanticTokens.some((token) => sourceSemanticTokens.includes(token))
          && aliasScore < 90) {
        return false;
      }

      if (targetShouldUseNumericAutofillValue(targetName)) {
        const split = splitAutofillValueAndUnit(candidate.value);
        if (split.numeric == null || isNaN(split.numeric)) return false;
      }

      const sourceIsOnlyContext = isAutofillContextOnlyName(sourceName);
      if (sourceIsOnlyContext && targetSemanticTokens.length && aliasScore < 90) return false;
      return true;
    }

    function commonAutofillTokenScore(targetName, sourceName) {
      const targetTokens = autofillParameterTokens(targetName);
      const sourceTokens = autofillParameterTokens(sourceName);
      if (!targetTokens.length || !sourceTokens.length) return 0;
      let score = 0;
      targetTokens.forEach((token) => {
        if (sourceTokens.includes(token)) score += 14;
      });
      return Math.min(score, 42);
    }

    function autofillShortNameScore(targetName, sourceName) {
      const targetShort = stripEquipmentPrefixForAutofill(targetName);
      const sourceShort = stripEquipmentPrefixForAutofill(sourceName);
      if (!targetShort || !sourceShort || targetShort.length < 2 || sourceShort.length < 2) return 0;
      if (targetShort === sourceShort) return 90;
      if (targetShort.length >= 2 && sourceShort.includes(targetShort)) return 84;
      if (sourceShort.length >= 2 && targetShort.includes(sourceShort)) return 80;
      return 0;
    }

    function autofillModelContext(result) {
      return [
        result && result.selected_model,
        result && result.template_name,
        result && result.selected_full_node_path,
        currentSelection && currentSelection.selected_model,
        currentSelection && currentSelection.template_name,
        currentSelection && currentSelection.selected_full_node_path
      ].filter(Boolean).join(' ');
    }

    function autofillNameScore(param, candidate, contextText) {
      const targetNames = [
        param && param.name,
        param && param.unit ? String(param.name || '') + '[' + param.unit + ']' : ''
      ].filter(Boolean);
      const sourceName = candidate && candidate.key;
      if (!sourceName) return 0;

      let score = 0;
      targetNames.forEach((targetName) => {
        const target = compactAutofillName(targetName);
        const source = compactAutofillName(sourceName);
        if (!target || !source) return;
        score = Math.max(score, autofillAliasScore(targetName, sourceName));
        if (target === source) score = Math.max(score, 100);
        if (target.length >= 3 && source.includes(target)) score = Math.max(score, 88);

        const sourceIsOnlyContext = autofillContextTokens(sourceName).some((token) => token === source);
        if (source.length >= 3 && target.includes(source) && !sourceIsOnlyContext) {
          score = Math.max(score, 82);
        }

        const targetShort = stripEquipmentPrefixForAutofill(targetName);
        const sourceShort = stripEquipmentPrefixForAutofill(sourceName);
        const targetTokens = autofillContextTokens(targetName);
        const sourceTokens = autofillContextTokens(sourceName);
        const hasSharedContext = targetTokens.some((token) => sourceTokens.includes(token));
        const contextTokens = autofillContextTokens(contextText);
        const hasModelContext = sourceTokens.some((token) => contextTokens.includes(token))
          || targetTokens.some((token) => contextTokens.includes(token));
        score = Math.max(score, autofillShortNameScore(targetName, sourceName));
        const tokenScore = commonAutofillTokenScore(targetName, sourceName);
        if (tokenScore > 0) {
          score = Math.max(score, (hasSharedContext || hasModelContext ? 60 : 50) + tokenScore);
        }
      });
      return score + (candidate.scoreBias || 0);
    }

    function convertAutofillValue(value, sourceUnit, targetUnit) {
      const split = splitAutofillValueAndUnit(value);
      if (split.numeric == null || isNaN(split.numeric)) return stripMarkdown(value);
      let source = normalizeAutofillUnit(sourceUnit || split.unit);
      const target = normalizeAutofillUnit(targetUnit);
      if (!source && target === 'm' && split.numeric >= 100) {
        source = 'mm';
      }
      if (!source || !target || source === target) return split.value;
      const lengthToMeter = { mm: 0.001, cm: 0.01, m: 1 };
      if (Object.prototype.hasOwnProperty.call(lengthToMeter, source)
          && Object.prototype.hasOwnProperty.call(lengthToMeter, target)) {
        const meters = split.numeric * lengthToMeter[source];
        const converted = meters / lengthToMeter[target];
        return String(Number(converted.toFixed(6)));
      }
      return split.value;
    }

    function normalizeAutofillOption(value) {
      return stripMarkdown(value)
        .replace(/\s+/g, '')
        .replace(/[×*]/g, 'x')
        .toLowerCase();
    }

    function resolveAutofillOptionValue(model, param, value, settings = {}) {
      if (!model || !param || !model.resolveValueList) return value;
      const valueOptions = model.resolveValueList(param);
      if (!Array.isArray(valueOptions) || !valueOptions.length) return value;
      const raw = stripMarkdown(value);
      const exact = valueOptions.find((option) => String(option).trim() === raw);
      if (exact != null) return String(exact);
      const normalized = normalizeAutofillOption(raw);
      const normalizedMatch = valueOptions.find((option) => normalizeAutofillOption(option) === normalized);
      if (normalizedMatch != null) return String(normalizedMatch);
      const numeric = parseFloat(raw);
      if (!isNaN(numeric)) {
        const numericMatch = valueOptions.find((option) => {
          const optionNumber = parseFloat(String(option).trim());
          return !isNaN(optionNumber) && Math.abs(optionNumber - numeric) < 0.000001;
        });
        if (numericMatch != null) return String(numericMatch);
        // 设备选型/参数提取是权威输入。即使 740 不在模板旧候选项中，也不能吸附回 450。
        if (settings.preserveUnlisted) return raw;
        const numericOptions = valueOptions
          .map((option) => ({ raw: String(option).trim(), num: parseFloat(String(option).trim()) }))
          .filter((item) => !isNaN(item.num));
        if (numericOptions.length) {
          let nearest = numericOptions[0];
          let nearestDiff = Math.abs(nearest.num - numeric);
          numericOptions.forEach((item) => {
            const diff = Math.abs(item.num - numeric);
            if (diff < nearestDiff) {
              nearest = item;
              nearestDiff = diff;
            }
          });
          return nearest.raw;
        }
        return null;
      }
      return settings.preserveUnlisted ? raw : null;
    }

    function findAutofillCandidateForParam(param, candidates, contextText) {
      if (!param || param.grid == null) return null;
      const targetCanonicalKey = canonicalAutofillKey(param.name);
      if (targetCanonicalKey) {
        const exact = (candidates || [])
          .filter((candidate) => candidate && candidate.canonicalKey === targetCanonicalKey)
          .filter((candidate) => isAutofillCandidateAllowedForParam(param, candidate, contextText)
            || autofillAliasScore(param.name, candidate.key) >= 90
            || comparableAutofillName(param.name) === comparableAutofillName(candidate.key))
          .map((candidate, index) => ({
            candidate,
            score: Number(candidate.sourceRank || 0) + Number(candidate.scoreBias || 0) + (candidate.sourceUnit ? 5 : 0) - index * 0.001
          }))
          .sort((a, b) => b.score - a.score);
        if (exact.length) return exact[0].candidate;
      }
      const targetParamTokens = autofillParameterTokens(param.name);
      if (!targetParamTokens.length) return null;
      const scored = (candidates || [])
        .filter((candidate) => isAutofillCandidateAllowedForParam(param, candidate, contextText))
        .map((candidate) => ({
          candidate,
          score: autofillNameScore(param, candidate, contextText)
        }))
        .filter((item) => item.score >= 78)
        .sort((a, b) => b.score - a.score);
      if (!scored.length) return null;
      const best = scored[0];
      const second = scored[1];
      if (second && best.score - second.score < 6
          && String(best.candidate.value) !== String(second.candidate.value)) {
        return null;
      }
      return best.candidate;
    }

    function applyEquipmentSelectionAutofill(model, result) {
      if (!model || !Array.isArray(model.params)) return { applied: {}, debug: {} };
      const explicitResult = typeof applyExplicitEquipmentMapping === 'function'
        ? applyExplicitEquipmentMapping(model, result)
        : { applied: {}, debug: {} };
      const applied = Object.assign({}, explicitResult.applied || {});
      const debug = Object.assign({}, explicitResult.debug || {});
      const candidates = collectAutofillCandidates(result);
      const contextText = autofillModelContext(result);
      model.params.forEach((param) => {
        if (!param || param.grid == null || !model.isPotentiallyVisible(param)) return;
        if (model.isUserTouched && model.isUserTouched(param.grid)) return;
        const paramKey = param.name || String(param.grid);
        if (applied[paramKey]) return;
        const candidate = findAutofillCandidateForParam(param, candidates, contextText);
        if (!candidate) return;
        const value = convertAutofillValue(
          candidate.value,
          candidate.sourceUnit,
          param.unit || parseUnitFromParameterName(param.name)
        );
        const resolvedValue = resolveAutofillOptionValue(model, param, value, { preserveUnlisted: true });
        if (!isValidAutofillValue(resolvedValue)) return;
        model.setValue(param.grid, resolvedValue, { silent: true, userTouched: true });
        applied[paramKey] = {
          value: resolvedValue,
          sourceValue: candidate.value,
          source: candidate.key,
          sourceUnit: candidate.sourceUnit || '',
          mapping: 'fuzzy'
        };
      });
      if (Object.keys(applied).length && model.resolveAllDefaults) {
        model.resolveAllDefaults();
      }
      debug.appliedCount = Object.keys(applied).length;
      return { applied, debug };
    }

    function syncAutofilledModelParametersToSelection(model, persist = false) {
      if (!model || !Array.isArray(model.params)) return;
      if (!currentSelection) currentSelection = {};
      const parameters = {};
      const parameterOrder = [];
      model.params.forEach((p) => {
        if (!p || !p.name || p.grid == null || !model.isPotentiallyVisible(p)) return;
        if (parameterOrder.includes(p.name)) return;
        parameters[p.name] = model.getValue(p.grid);
        parameterOrder.push(p.name);
      });
      currentSelection.parameters = filterParameterMap(parameters);
      currentSelection.parameter_rows = [Object.assign({}, currentSelection.parameters)];
      currentSelection.parameter_order = parameterOrder;
      if (persist) updateJiavuActions(currentSelection);
    }

    function splitMarkdownTableLine(line) {
      const trimmed = String(line || '').trim();
      if (!trimmed.startsWith('|') || !trimmed.endsWith('|')) return [];
      return trimmed
        .slice(1, -1)
        .split('|')
        .map((cell) => stripMarkdown(cell.replace(/\\\|/g, '|')).trim());
    }

    function isMarkdownTableSeparator(line) {
      const cells = splitMarkdownTableLine(line);
      return cells.length > 1 && cells.every((cell) => /^:?-+:?$/.test(cell.replace(/\s+/g, '')));
    }

    function findHeaderIndex(headers, names) {
      return headers.findIndex((header) => {
        const normalized = normalizeParamKey(header);
        return names.some((name) => normalized.includes(normalizeParamKey(name)));
      });
    }

    function appendRequirementPart(parts, label, value) {
      const text = stripMarkdown(value);
      if (!text || text === '-' || text === '无' || text === '待填写') return;
      const fragments = text
        .split(/[；;]/)
        .map((item) => stripMarkdown(item).trim())
        .filter((item) => item && item !== '-' && item !== '无' && item !== '待填写');
      (fragments.length ? fragments : [text]).forEach((fragment) => {
        const labeled = label && !/[：:]/.test(fragment) ? `${label}：${fragment}` : fragment;
        const normalized = normalizeParamKey(labeled);
        if (!normalized || parts.some((part) => normalizeParamKey(part) === normalized)) return;
        parts.push(labeled);
      });
    }

    function addRequirement(requirements, key, detail) {
      const name = stripMarkdown(key);
      const text = stripMarkdown(detail);
      if (!name || !text || isIgnoredParameterName(name)) return;
      if (!requirements[name]) {
        requirements[name] = [];
      }
      appendRequirementPart(requirements[name], '', text);
    }

    function parseMarkdownParameterRequirements(markdown) {
      const requirements = {};
      const lines = String(markdown || '').replace(/\r\n/g, '\n').split('\n');
      for (let index = 0; index < lines.length - 2; index += 1) {
        const headers = splitMarkdownTableLine(lines[index]);
        if (!headers.length || !isMarkdownTableSeparator(lines[index + 1])) continue;
        const nameIndex = findHeaderIndex(headers, ['参数名称', '参数名', '参数', '名称', '字段']);
        if (nameIndex < 0) continue;
        const valueIndex = findHeaderIndex(headers, ['当前值', '参数值', '取值', '值']);
        const noteIndexes = headers
          .map((header, headerIndex) => ({ header, headerIndex }))
          .filter(({ headerIndex }) => headerIndex !== nameIndex && headerIndex !== valueIndex)
          .filter(({ header }) => {
            const normalized = normalizeParamKey(header);
            return ['说明', '备注', '范围', '取值范围', '步长', '默认', '默认值', '示例', '枚举', '单位', '约束', '要求']
              .some((name) => normalized.includes(normalizeParamKey(name)));
          });
        let rowIndex = index + 2;
        while (rowIndex < lines.length) {
          const cells = splitMarkdownTableLine(lines[rowIndex]);
          if (!cells.length) break;
          const name = cells[nameIndex] || '';
          const parts = [];
          if (valueIndex >= 0) appendRequirementPart(parts, '当前值', cells[valueIndex]);
          noteIndexes.forEach(({ header, headerIndex }) => appendRequirementPart(parts, header, cells[headerIndex]));
          if (name && parts.length && !isIgnoredParameterName(name)) {
            requirements[name] = parts;
          }
          rowIndex += 1;
        }
        index = rowIndex;
      }

      const bulletPattern = /^[-*]\s+(?:\*\*)?([^*：:\n\r]+?)(?:\*\*)?\s*[：:]\s*([^\n\r]+)/gm;
      let match;
      while ((match = bulletPattern.exec(String(markdown || ''))) !== null) {
        const key = stripMarkdown(match[1]);
        const value = stripMarkdown(match[2]);
        if (!key || !value) continue;
        if (/[备注范围步长默认示例枚举单位约束要求说明]/.test(value)) {
          addRequirement(requirements, key, value);
        }
      }
      return Object.fromEntries(Object.entries(requirements).map(([key, parts]) => [key, parts.join('；')]));
    }

    function parameterRequirementRows(result) {
      const rowsByName = {};
      const append = (name, detail) => {
        const key = stripMarkdown(name);
        const text = stripMarkdown(detail);
        if (!key || !text || isIgnoredParameterName(key)) return;
        if (!rowsByName[key]) rowsByName[key] = [];
        appendRequirementPart(rowsByName[key], '', text);
      };
      Object.entries(result.parameter_requirements || {}).forEach(([key, value]) => append(key, value));
      Object.entries(result.answer_markdown ? parseMarkdownParameterRequirements(result.answer_markdown) : {})
        .forEach(([key, value]) => append(key, value));
      return Object.entries(rowsByName).map(([key, parts]) => `${key}：${parts.join('；')}`);
    }

    function getMarkdownTableHeaders(table) {
      const headers = [];
      if (!table) return headers;
      table.querySelectorAll('thead th').forEach((th) => {
        headers.push(stripMarkdown(th.textContent || ''));
      });
      if (headers.length) return headers;
      const firstRow = table.querySelector('tbody tr') || table.querySelector('tr');
      if (!firstRow) return headers;
      firstRow.querySelectorAll('th, td').forEach((cell) => {
        headers.push(stripMarkdown(cell.textContent || ''));
      });
      return headers;
    }

    function headerEquals(header, name) {
      return stripMarkdown(header) === stripMarkdown(name);
    }

    function findExactHeaderIndex(headers, names) {
      return headers.findIndex((header) => names.some((name) => headerEquals(header, name)));
    }

    function detectEditableResultTable(headers) {
      // “反推与正推校验”中的用户目标也是可修正的计算结果。
      // 接入相同的草稿确认流程后，错误目标提交后仍可在下一轮校验表中覆盖修正。
      const inverseNameIndex = findExactHeaderIndex(headers, ['用户修改参数']);
      const inverseValueIndex = findExactHeaderIndex(headers, ['用户目标']);
      if (inverseNameIndex >= 0 && inverseValueIndex >= 0) {
        return {
          kind: 'equipment_result',
          role: 'inverse_validation',
          valueIndex: inverseValueIndex,
          nameIndex: inverseNameIndex
        };
      }

      const correctionNameIndex = findExactHeaderIndex(headers, ['参数']);
      const correctionValueIndex = findExactHeaderIndex(headers, ['参数值']);
      const correctionIssueIndex = findExactHeaderIndex(headers, ['校验问题']);
      if (correctionNameIndex >= 0 && correctionValueIndex >= 0 && correctionIssueIndex >= 0) {
        return {
          kind: 'equipment_result',
          role: 'correction',
          valueIndex: correctionValueIndex,
          nameIndex: correctionNameIndex
        };
      }

      const valueIndex = findHeaderIndex(headers, AUTOFILL_VALUE_HEADERS.concat(['设备型号/参数', '型号/参数', '设备型号']));
      if (valueIndex < 0) return null;

      const hasParamId = findHeaderIndex(headers, ['参数标识', '参数id', '参数ID']) >= 0;
      const hasEvidence = findHeaderIndex(headers, ['证据溯源', '证据来源', '依据', '取得途径', '来源', '来源/证据']) >= 0;
      const hasParamChineseName = findHeaderIndex(headers, ['参数中文名称']) >= 0;
      const hasDevicePair = findHeaderIndex(headers, ['设备名称']) >= 0
        && findHeaderIndex(headers, ['设备型号/参数', '型号/参数', '设备型号']) >= 0;

      if (hasParamId || (hasParamChineseName && hasEvidence)) {
        return {
          kind: 'param_extract',
          valueIndex,
          nameIndex: findHeaderIndex(headers, ['参数标识', '参数id', '参数ID', '参数中文名称', '参数名称', '参数名'])
        };
      }

      const equipmentNameIndex = findExactHeaderIndex(headers, ['参数', '参数名称', '设备名称']);
      const equipmentValueIndex = findExactHeaderIndex(headers, ['参数值', '设备型号/参数', '型号/参数']);
      const hasFollowUp = findHeaderIndex(headers, ['后续应用', '应用说明', '应用']) >= 0;
      if (equipmentNameIndex >= 0 && (equipmentValueIndex >= 0 || valueIndex >= 0) && (hasFollowUp || equipmentValueIndex >= 0)) {
        return {
          kind: 'equipment_result',
          valueIndex: equipmentValueIndex >= 0 ? equipmentValueIndex : valueIndex,
          nameIndex: equipmentNameIndex
        };
      }

      if (hasDevicePair) {
        return {
          kind: 'equipment_result',
          valueIndex: findHeaderIndex(headers, ['设备型号/参数', '型号/参数', '设备型号']),
          nameIndex: findHeaderIndex(headers, ['设备名称'])
        };
      }

      return null;
    }

    function isEditableParameterResultTable(headers) {
      return detectEditableResultTable(headers);
    }

    function normalizeEditableCellValue(value) {
      const text = stripMarkdown(value);
      if (!text || text.toLowerCase() === 'null') return '';
      return text;
    }

    // 《一步上仓设备选型逻辑0720》最终下拉值参数汇总。
    // 文档明确“初期默认”的参数配置 defaultValue；只给出候选值而没有默认规则的参数保持空值。
    // 候选值使用模型接口要求的枚举文本；数值型候选不附单位，原结果中的 mm/°/种/组会在归一化时去除。
    const EDITABLE_PARAMETER_DROPDOWNS = {
      seal_type: {
        name: '密封形式',
        defaultValue: '密封梁',
        options: ['密封梁', '密封板']
      },
      slag_well_count: {
        name: '渣井口数',
        defaultValue: '',
        options: ['2', '3', '4', '5']
      },
      same_horizontal_spacing: {
        name: '横向立柱间距是否相同',
        defaultValue: '是',
        options: ['是', '否']
      },
      has_slag_well_platform: {
        name: '是否有渣井平台',
        defaultValue: '是',
        options: ['是', '否']
      },
      slag_well_ladder_type: {
        name: '渣井斜梯类型',
        defaultValue: '普通斜梯',
        options: ['普通斜梯', '搭接斜梯']
      },
      slag_well_ladder_position: {
        name: '渣井斜梯定位类型',
        defaultValue: '左下右上',
        options: ['左下右上', '左上右下']
      },
      hydraulic_gate_model: {
        name: '液压破碎关断门组件型号',
        aliases: ['液压破碎关断门型号'],
        defaultValue: '',
        options: [
          'YPM225', 'YPM226', 'YPM235', 'YPM236',
          'YPM325', 'YPM326', 'YPM335', 'YPM336',
          'YPM425', 'YPM426', 'YPM435', 'YPM436',
          'YPM525', 'YPM526', 'YPM535', 'YPM536'
        ]
      },
      gate_expansion_type: {
        name: '膨胀节类型',
        defaultValue: '',
        options: ['波纹膨胀节', '反法兰小膨胀节']
      },
      gate_platform_type: {
        name: '关断门平台类型',
        defaultValue: '无平台',
        options: ['无平台', '普通平台', '移动检修平台']
      },
      gate_ladder_type: {
        name: '关断门斜梯类型',
        defaultValue: '左下右上',
        options: ['上方两侧', '下方两侧', '左侧上下两端', '右侧上下两端', '左下右上', '右上左下']
      },
      gate_connection_type: {
        name: '连接口形式',
        defaultValue: '法兰连接口',
        options: ['法兰连接口', '法兰+观察窗连接口']
      },
      conveyor_shell_model: {
        name: '鳞斗干渣机机壳型号',
        defaultValue: '',
        options: ['GLD8', 'GLD12', 'GLD16', 'GLD18', 'GLD20']
      },
      conveyor_tail_shell_length: {
        name: '鳞斗干渣机尾段机壳长度',
        defaultValue: '4860',
        options: ['4860', '5047']
      },
      conveyor_horizontal_length_type_count: {
        name: '鳞斗干渣机水平段机壳长度种类数量',
        defaultValue: '2',
        options: ['1', '2', '3']
      },
      conveyor_horizontal_standard_length: {
        name: '鳞斗干渣机水平段标准节机壳长度',
        defaultValue: '4000',
        options: ['4000', '4800', '6000']
      },
      conveyor_horizontal_adjust_length: {
        name: '鳞斗干渣机水平段后端调整节机壳长度',
        defaultValue: '',
        options: ['4000', '4800', '6000']
      },
      conveyor_slope_length_type_count: {
        name: '鳞斗干渣机斜升段机壳长度种类数量',
        defaultValue: '2',
        options: ['1', '2', '3']
      },
      conveyor_slope_standard_length: {
        name: '鳞斗干渣机斜升段标准节机壳长度',
        defaultValue: '4000',
        options: ['4000', '4800', '6000']
      },
      conveyor_slope_adjust_length: {
        name: '鳞斗干渣机斜升段后端调整节机壳长度',
        defaultValue: '',
        options: ['4000', '4800', '6000']
      },
      conveyor_head_shell_length: {
        name: '鳞斗干渣机头部机壳长度',
        aliases: ['头部机壳长度'],
        defaultValue: '',
        options: ['5000', '3854']
      },
      conveyor_head_reducer_position: {
        name: '鳞斗干渣机头部机壳减速器位置',
        defaultValue: '右侧',
        options: ['左侧', '右侧']
      },
      conveyor_head_negative_dust: {
        name: '鳞斗干渣机头部机壳是否有负压吸尘',
        defaultValue: '无',
        options: ['无', '有']
      },
      conveyor_angle: {
        name: '鳞斗干渣机干渣机倾角',
        aliases: ['干渣机倾角', '干渣机角度'],
        defaultValue: '',
        options: ['30', '35', '38', '40', '42', '45']
      },
      conveyor_head_platform_ladder_type: {
        name: '鳞斗干渣机头部平台斜梯类型',
        defaultValue: '直爬梯',
        options: ['直爬梯', '斜梯']
      },
      conveyor_head_platform_ladder_position: {
        name: '鳞斗干渣机头部平台斜梯位置',
        defaultValue: '左侧',
        options: ['左侧', '右侧']
      },
      conveyor_ground_pillar_component_count: {
        name: '干渣机托架组件地面立柱组件数量',
        defaultValue: '3',
        options: ['1', '2', '3']
      },
      conveyor_pillar1_type: {
        name: '干渣机托架组件立柱1类型',
        defaultValue: '单排立柱',
        options: ['单排立柱', '双排立柱']
      },
      conveyor_pillar2_type: {
        name: '干渣机托架组件立柱2类型',
        defaultValue: '单排立柱',
        options: ['单排立柱', '双排立柱']
      },
      conveyor_pillar3_type: {
        name: '干渣机托架组件立柱3类型',
        defaultValue: '双排立柱',
        options: ['单排立柱', '双排立柱']
      },
      d1: {
        name: '干渣仓仓体直径',
        aliases: ['渣仓直径', '干渣仓直径'],
        defaultValue: '',
        options: ['6000', '7000', '8000', '9000', '10000']
      },
      cone_included_angle: {
        name: '干渣仓仓体锥段角度',
        aliases: ['干渣仓锥斗夹角', '锥段角度'],
        defaultValue: '70',
        options: ['60', '70']
      },
      silo_top_cut: {
        name: '干渣仓仓顶是否切角',
        aliases: ['干渣仓仓顶是否有切角', '渣仓仓体仓顶是否切角', '渣仓仓体仓顶是否有切角'],
        defaultValue: '切角',
        options: ['不切角', '切角']
      },
      silo_platform_type: {
        name: '渣仓转运层平台类型',
        defaultValue: '有出车端和后端',
        options: ['无出车端和后端', '只有出车端', '只有后端', '有出车端和后端']
      },
      unloading_type: {
        name: '干渣仓卸料形式',
        defaultValue: '双口卸料',
        options: ['单口卸料', '双口卸料', '单口+事故排渣管', '双口+事故排渣管']
      },
      silo_emergency_pipe_count: {
        name: '干渣仓事故排渣管数量',
        defaultValue: '1',
        options: ['1', '2']
      },
      silo_steel_frame_type: {
        name: '干渣仓钢支架类型',
        defaultValue: '直撑式',
        options: ['直撑式', '框架式']
      },
      silo_ladder_component_type: {
        name: '干渣仓扶梯组件类型',
        defaultValue: '常规扶梯组件',
        options: ['常规扶梯组件', '转运层扶梯组件']
      },
      transfer_ladder_position_type: {
        name: '转运层扶梯组件定位类型',
        defaultValue: '右侧',
        options: ['右侧', '左侧', '中间']
      },
      silo_frame_h_beam_spec: {
        name: '框架式钢支架顶部H型钢横撑规格',
        defaultValue: 'H588×300',
        options: ['H390×300', 'H588×300', 'H700×300']
      },
      silo_ladder_position: {
        name: '干渣仓扶梯位置类型',
        defaultValue: '左侧',
        options: ['左侧', '右侧']
      },
      silo_ladder_pillar_position: {
        name: '干渣仓扶梯立柱定位类型',
        defaultValue: '靠近出车端',
        options: ['靠近出车端', '靠近后端']
      },
      single_roll_crusher_model: {
        name: '单辊碎渣机型号',
        defaultValue: '',
        options: ['GDGS586', 'GDGS587', 'GDGS588']
      },
      crusher_reducer_type: {
        name: '单辊碎渣机减速机左右型',
        defaultValue: '左型',
        options: ['左型', '右型']
      },
      crusher_layout_type: {
        name: '碎渣机组件碎渣机布置形式',
        defaultValue: '平行于轴移出',
        options: ['平行于轴移出', '垂直于轴移出']
      },
      crusher_deployment_type: {
        name: '碎渣机组件碎渣机类型',
        defaultValue: '单台布置',
        options: ['单台布置', '一备一运', '对向布置']
      },
      conveyor_silo_connection_type: {
        name: '碎渣机连接装置连接管形式',
        defaultValue: '普通连接管',
        options: ['普通连接管', '连接管+事故排渣管', '连接管+普通三通', '连接管+倒V三通']
      },
      dry_unloading_type: {
        name: '干式卸料类型',
        defaultValue: '大型除尘器',
        options: ['自身除尘器', '自身风机', '大型除尘器']
      },
      unloading_double_position: {
        name: '双口卸料定位',
        defaultValue: '干式左侧湿式右侧',
        options: ['干式左侧湿式右侧', '湿式左侧干式右侧']
      },
      single_unloading_type: {
        name: '单口卸料类型',
        defaultValue: '单口干式卸料',
        options: ['单口干式卸料', '单口湿式卸料']
      },
      wet_mixer_reducer_type: {
        name: '湿式卸料双轴搅拌机驱动机构左右型',
        defaultValue: '左型',
        options: ['左型', '右型']
      },
      bag_filter_position: {
        name: '干卸料布袋除尘定位位置',
        defaultValue: '出车端左侧',
        options: ['出车端左侧', '后端左侧', '出车端右侧', '后端右侧']
      }
    };

    // 0720 最终结果权限采用 fail-closed：只有清单明确允许修改的名称才可进入草稿。
    // 参数提取表、计算失败修正表和反推校验表有各自的编辑职责，不受此白名单限制。
    const EDITABLE_FINAL_RESULT_NAMES = new Set([
      '渣井型号', '干渣井型号', '液压破碎关断门型号', '液压破碎关断门组件型号',
      '鳞斗干渣机型号', '单辊碎渣机型号', '干渣仓型号', '渣仓型号',
      '干式卸料型号', '湿式卸料型号', '下联箱标高', '锅炉梳型密封板长度',
      '锅炉梳型密封板宽度', '锅炉梳型密封板连接螺栓高度差', '密封形式',
      '渣井口数', '渣井口间距', '渣井偏心距离', '渣井平台左右端宽度',
      '渣井平台前端宽度', '渣井平台后端宽度', '渣井斜梯类型', '渣井斜梯定位类型',
      '关断门平台类型', '鳞斗干渣机机壳型号', '鳞斗干渣机尾段机壳长度',
      '鳞斗干渣机弧段定位距离', '鳞斗干渣机干渣机倾角', '鳞斗干渣机头部平台高度',
      '干渣机托架组件地面立柱组件数量', '干渣仓中心线至锅炉中心线间距',
      '干渣仓仓体直径', '干渣仓仓体锥段角度', '渣仓转运层平台高度',
      '渣仓转运层平台类型', '干渣仓卸料形式', '干渣仓扶梯组件类型',
      '单辊碎渣机减速机左右型', '车间内外地面高度差'
    ].map((name) => normalizeParamKey(name)));

    function equipmentResultRowIsEditable(headers, cells, meta) {
      if (!meta || meta.kind !== 'equipment_result') return true;
      if (meta.role === 'inverse_validation' || meta.role === 'correction') return true;
      const labelIndex = meta.nameIndex >= 0
        ? meta.nameIndex
        : findExactHeaderIndex(headers, ['参数', '参数名称', '设备名称']);
      const label = labelIndex >= 0 && cells[labelIndex]
        ? normalizeEditableCellValue(cells[labelIndex].textContent || '')
        : '';
      return EDITABLE_FINAL_RESULT_NAMES.has(normalizeParamKey(label));
    }

    function editableParameterDropdown(paramId, paramName, tableKind = 'param_extract') {
      const id = String(paramId || '').trim().toLowerCase();
      const allowedForTable = (definition) => Boolean(definition)
        && (!definition.resultOnly || tableKind === 'equipment_result');
      if (id) {
        const byId = EDITABLE_PARAMETER_DROPDOWNS[id] || null;
        return allowedForTable(byId) ? byId : null;
      }
      const normalizedName = normalizeParamKey(paramName);
      if (!normalizedName) return null;
      return Object.values(EDITABLE_PARAMETER_DROPDOWNS).find((definition) => {
        if (!allowedForTable(definition)) return false;
        const names = [definition.name].concat(definition.aliases || []);
        return names.some((name) => normalizeParamKey(name) === normalizedName);
      }) || null;
    }

    function isMissingEditableDropdownValue(value) {
      const text = normalizeEditableCellValue(value).toLowerCase();
      return !text || [
        '-', 'null', 'none', 'undefined', '未识别', '未提取', '未找到',
        '待填写', '待补充', '待确认', '未知', '缺失', 'n/a', 'na'
      ].includes(text);
    }

    function normalizeEditableDropdownCandidate(definition, value) {
      if (!definition || isMissingEditableDropdownValue(value)) return '';
      const text = normalizeEditableCellValue(value);
      const exact = definition.options.find((option) => option === text);
      if (exact) return exact;
      const compact = text.replace(/\s+/g, '').replace(/＋/g, '+');

      // 数值枚举统一去掉结果表中的单位后缀。示例：4000mm→4000、2种→2、3组→3、38°→38。
      const numericMatch = compact.match(/^-?(\d+(?:\.\d+)?)(?:mm|m³|m3|°|度|种|组|节|口|个|台|套)?$/i);
      if (numericMatch && definition.options.includes(numericMatch[1])) return numericMatch[1];

      if (definition === EDITABLE_PARAMETER_DROPDOWNS.d1) {
        const diameter = compact.match(/(\d+(?:\.\d+)?)(mm|毫米|m|米)?/i);
        if (diameter) {
          let number = Number(diameter[1]);
          if ((diameter[2] || '').toLowerCase() === 'm' || diameter[2] === '米' || number <= 10) number *= 1000;
          const candidate = String(Math.round(number));
          if (definition.options.includes(candidate)) return candidate;
        }
      }

      if (definition === EDITABLE_PARAMETER_DROPDOWNS.cone_included_angle) {
        const threshold = compact.match(/(?:不小于|不得小于|至少|≥|>=)(\d+(?:\.\d+)?)/);
        if (threshold && Number(threshold[1]) <= 70) return '70';
        const upper = compact.match(/(?:不大于|不得大于|至多|≤|<=)(\d+(?:\.\d+)?)/);
        if (upper && Number(upper[1]) >= 60) return '60';
      }

      if (definition === EDITABLE_PARAMETER_DROPDOWNS.hydraulic_gate_model) {
        const model = compact.toUpperCase().match(/YPM(?:225|226|235|236|325|326|335|336|425|426|435|436|525|526|535|536)/);
        if (model && definition.options.includes(model[0])) return model[0];
      }

      if (definition === EDITABLE_PARAMETER_DROPDOWNS.conveyor_shell_model) {
        const model = compact.toUpperCase().match(/GLD(?:8|12|16|18|20)/);
        if (model && definition.options.includes(model[0])) return model[0];
      }

      if (definition === EDITABLE_PARAMETER_DROPDOWNS.single_roll_crusher_model) {
        const model = compact.toUpperCase().match(/GDGS(?:586|587|588)/);
        if (model && definition.options.includes(model[0])) return model[0];
      }

      if (definition === EDITABLE_PARAMETER_DROPDOWNS.unloading_type) {
        const hasAccidentPipe = /事故排渣管/.test(compact);
        if (hasAccidentPipe && /双口|两口|双卸料口/.test(compact)) return '双口+事故排渣管';
        if (hasAccidentPipe && /单口|一口|单卸料口/.test(compact)) return '单口+事故排渣管';
        if (/双口|两口|双卸料口/.test(compact)) return '双口卸料';
        if (/单口|一口|单卸料口/.test(compact)) return '单口卸料';
      }

      if (definition === EDITABLE_PARAMETER_DROPDOWNS.conveyor_silo_connection_type) {
        if (/事故排渣管/.test(compact)) return '连接管+事故排渣管';
        if (/倒[VvＶｖ](?:型|形)?三通|倒三通/.test(compact)) return '连接管+倒V三通';
        if (/普通三通/.test(compact) || /三通/.test(compact)) return '连接管+普通三通';
        if (/普通连接管|连接管|直管/.test(compact)) return '普通连接管';
      }

      if (definition === EDITABLE_PARAMETER_DROPDOWNS.gate_platform_type) {
        if (/移动检修平台|移动平台/.test(compact)) return '移动检修平台';
        if (/无平台|不设平台|不设置平台/.test(compact)) return '无平台';
        if (/普通平台|固定平台|有平台/.test(compact)) return '普通平台';
      }

      if (definition === EDITABLE_PARAMETER_DROPDOWNS.silo_platform_type) {
        const noDelivery = /无出车端|不设出车端|没有出车端/.test(compact);
        const noBack = /无后端|不设后端|没有后端/.test(compact);
        if (/无出车端(?:和|及|、)?后端|出车端(?:和|及|、)?后端(?:均|都)?不设/.test(compact)
          || (noDelivery && noBack)) return '无出车端和后端';
        if (/只有出车端|仅(?:有)?出车端/.test(compact)
          || (/有出车端/.test(compact) && noBack)) return '只有出车端';
        if (/只有后端|仅(?:有)?后端/.test(compact)
          || (noDelivery && /有后端/.test(compact))) return '只有后端';
        if (/有出车端(?:和|及|、)?后端|出车端(?:和|及|、)?后端(?:均|都)?有/.test(compact)) {
          return '有出车端和后端';
        }
      }

      if (definition === EDITABLE_PARAMETER_DROPDOWNS.silo_ladder_component_type) {
        if (/转运层扶梯组件|转运层扶梯|转运扶梯组件/.test(compact)) return '转运层扶梯组件';
        if (/常规扶梯组件|常规扶梯|普通扶梯组件/.test(compact)) return '常规扶梯组件';
      }

      if (definition === EDITABLE_PARAMETER_DROPDOWNS.same_horizontal_spacing
        || definition === EDITABLE_PARAMETER_DROPDOWNS.has_slag_well_platform) {
        if (/否|不相同|不同|不设|没有|无.*平台/.test(compact)) return '否';
        if (/是|相同|有平台|设置平台/.test(compact)) return '是';
      }

      if (definition === EDITABLE_PARAMETER_DROPDOWNS.silo_top_cut) {
        if (/不切角|否|不设切角|不需要切角/.test(compact)) return '不切角';
        if (/切角|是|需要切角/.test(compact)) return '切角';
      }

      if (definition === EDITABLE_PARAMETER_DROPDOWNS.conveyor_head_negative_dust) {
        if (/无|不设|没有/.test(compact)) return '无';
        if (/有|设置/.test(compact)) return '有';
      }

      if (definition === EDITABLE_PARAMETER_DROPDOWNS.conveyor_head_platform_ladder_type) {
        if (/直爬梯|直梯|爬梯/.test(compact)) return '直爬梯';
        if (/斜梯/.test(compact)) return '斜梯';
      }

      if (definition === EDITABLE_PARAMETER_DROPDOWNS.conveyor_head_platform_ladder_position) {
        if (/左侧|左边|左置/.test(compact)) return '左侧';
        if (/右侧|右边|右置/.test(compact)) return '右侧';
      }

      // 文本结果常带“初期默认/选型结果”等说明，优先匹配最长候选，避免“除尘器”等子串误选。
      const contained = definition.options
        .filter((option) => !/^\d+(?:\.\d+)?$/.test(option))
        .sort((a, b) => b.length - a.length)
        .find((option) => compact.includes(option.replace(/\s+/g, '')));
      if (contained) return contained;

      const looseNumber = compact.match(/-?\d+(?:\.\d+)?/);
      if (looseNumber && definition.options.includes(looseNumber[0])) {
        return looseNumber[0];
      }
      return '';
    }

    function normalizeEditableDropdownValue(definition, value) {
      if (!definition) return '';
      const candidate = normalizeEditableDropdownCandidate(definition, value);
      if (candidate) return candidate;
      // 只有原始值确实缺失时才允许使用显式配置的初期默认值；
      // 文档给出但不属于候选列表的内容必须保持空白，等待用户选择。
      return isMissingEditableDropdownValue(value) && definition.options.includes(definition.defaultValue)
        ? definition.defaultValue
        : '';
    }

    function dropdownForEditableParameterRow(headers, cells, meta) {
      if (!meta) return null;
      if (meta.kind === 'equipment_result') {
        const labelIndex = meta.nameIndex >= 0
          ? meta.nameIndex
          : findExactHeaderIndex(headers, ['参数', '设备名称', '参数名称']);
        const label = labelIndex >= 0 && cells[labelIndex]
          ? normalizeEditableCellValue(cells[labelIndex].textContent || '')
          : '';
        return editableParameterDropdown('', label, 'equipment_result');
      }
      if (meta.kind !== 'param_extract') return null;
      const paramIdIndex = findHeaderIndex(headers, ['参数标识', '参数id', '参数ID']);
      const paramNameIndex = findHeaderIndex(headers, ['参数中文名称', '参数名称', '参数名']);
      const paramId = paramIdIndex >= 0 && cells[paramIdIndex]
        ? normalizeEditableCellValue(cells[paramIdIndex].textContent || '')
        : '';
      const paramName = paramNameIndex >= 0 && cells[paramNameIndex]
        ? normalizeEditableCellValue(cells[paramNameIndex].textContent || '')
        : '';
      return editableParameterDropdown(paramId, paramName, 'param_extract');
    }

    function editableTableRows(table) {
      if (!table) return [];
      const bodyRows = Array.from(table.querySelectorAll('tbody tr'));
      if (bodyRows.length) return bodyRows;
      return Array.from(table.querySelectorAll('tr')).filter((tr) => !tr.querySelector('th'));
    }

    function readEditableParameterTable(table) {
      const headers = getMarkdownTableHeaders(table);
      const meta = detectEditableResultTable(headers);
      if (!meta) return null;
      const rows = [];
      editableTableRows(table).forEach((tr) => {
        const cells = tr.querySelectorAll('td');
        const row = {};
        headers.forEach((header, index) => {
          const td = cells[index];
          if (!td) return;
          const control = td.querySelector('input.param-table-input, select.param-table-input');
          row[header] = control
            ? normalizeEditableCellValue(control.value)
            : normalizeEditableCellValue(td.textContent || '');
        });
        if (Object.values(row).some(Boolean)) rows.push(row);
      });
      return { headers, rows, meta };
    }

    function syncAllEditableParameterTablesToSelection() {
      let latestParameterRows = [];
      let latestEquipmentRows = [];
      let latestParameterIndex = -1;
      let latestEquipmentIndex = -1;
      if (!chat) return latestParameterRows;
      chat.querySelectorAll('table[data-editable-param-table="true"]').forEach((table, tableIndex) => {
        const parsed = readEditableParameterTable(table);
        if (!parsed || !parsed.rows.length) return;
        if (parsed.meta.kind === 'equipment_result') {
          const draftState = equipmentParameterDraftState(table, false);
          // 未确认草稿不能进入设计模型参数快照；确认后的服务端新结果才是权威版本。
          if (draftState && (draftState.editing || draftState.submitting)) return;
          latestEquipmentRows = parsed.rows;
          latestEquipmentIndex = tableIndex;
        } else {
          latestParameterRows = parsed.rows;
          latestParameterIndex = tableIndex;
        }
      });
      if (!latestParameterRows.length && !latestEquipmentRows.length) return latestParameterRows;
      if (!currentSelection) currentSelection = {};
      if (latestParameterRows.length) {
        currentSelection.parameter_rows = filterParameterRows(latestParameterRows);
        const params = Object.assign({}, currentSelection.parameters || {});
        latestParameterRows.forEach((row) => {
          const paramId = rowValueByHeader(row, ['参数标识', '参数id', '参数ID']);
          const paramName = rowValueByHeader(row, ['参数中文名称', '参数名称', '参数名']);
          const value = rowValueByHeader(row, AUTOFILL_VALUE_HEADERS);
          const deviceName = rowValueByHeader(row, ['设备名称']);
          const deviceValue = rowValueByHeader(row, ['设备型号/参数', '型号/参数', '设备型号']);
          const dropdown = editableParameterDropdown(paramId, paramName);
          if (dropdown && !value) {
            if (paramId) delete params[paramId];
            if (paramName) delete params[paramName];
          } else {
            if (paramId && value) params[paramId] = value;
            if (paramName && value) params[paramName] = value;
          }
          if (deviceName && deviceValue) params[deviceName] = deviceValue;
        });
        currentSelection.parameters = filterParameterMap(params);
      }
      if (latestEquipmentRows.length) {
        currentSelection.equipment_result_rows = filterParameterRows(latestEquipmentRows);
        const params = Object.assign({}, currentSelection.parameters || {});
        latestEquipmentRows.forEach((row) => {
          const label = rowValueByHeader(row, ['参数', '设备名称']);
          const value = rowValueByHeader(row, ['参数值', '设备型号/参数', '型号/参数', '数值', '值']);
          if (label && value) params[label] = value;
        });
        currentSelection.parameters = filterParameterMap(params);
      }
      // 这里只收集用户对表格的编辑，供下一次“开始计算”拼接输入。
      // 设计模型参数桥不得读取 DOM 表格，否则两次计算的表格可能被跨响应拼接。
      updateJiavuActions(currentSelection);
      return latestParameterRows.length ? latestParameterRows : latestEquipmentRows;
    }

    function syncEditableParameterTableFromDom(table) {
      const parsed = readEditableParameterTable(table);
      if (!parsed || !parsed.rows.length) return;
      const draftState = parsed.meta.kind === 'equipment_result'
        ? equipmentParameterDraftState(table, false)
        : null;
      if (draftState && (draftState.editing || draftState.submitting)) {
        updateEquipmentParameterDraftToolbar(table);
        return;
      }
      const bubble = table.closest('.bubble');
      if (bubble) {
        bubble.dataset.parameterRows = JSON.stringify(parsed.rows);
      }
      syncAllEditableParameterTablesToSelection();
    }

    function enhanceEditableParameterTables(container) {
      if (!container) return;
      container.querySelectorAll('table').forEach((table) => {
        const headers = getMarkdownTableHeaders(table);
        const meta = detectEditableResultTable(headers);
        if (!meta) return;
        if (table.dataset.editableParamTable === 'true') {
          editableTableRows(table).forEach((tr) => {
            const control = tr.querySelector('input.param-table-input, select.param-table-input');
            if (control) bindEditableParameterControl(table, tr, control);
          });
          if (meta.kind === 'equipment_result') ensureEquipmentParameterDraftToolbar(table);
          syncEditableParameterTableFromDom(table);
          return;
        }
        table.dataset.editableParamTable = 'true';
        table.dataset.editableTableKind = meta.kind;
        table.classList.add('editable-param-result-table');
        editableTableRows(table).forEach((tr) => {
          if (tr.querySelector('th')) return;
          const cells = tr.querySelectorAll('td');
          if (cells.length <= meta.valueIndex) return;
          const valueTd = cells[meta.valueIndex];
          if (valueTd.querySelector('input.param-table-input, select.param-table-input')) return;
          const rawValue = normalizeEditableCellValue(valueTd.textContent || '');
          const rowIsEditable = equipmentResultRowIsEditable(headers, cells, meta);
          valueTd.textContent = '';
          const dropdown = dropdownForEditableParameterRow(headers, cells, meta);
          let control;
          if (dropdown) {
            control = document.createElement('select');
            const emptyOption = document.createElement('option');
            emptyOption.value = '';
            emptyOption.textContent = '';
            control.appendChild(emptyOption);
            const sourceValue = normalizeEditableDropdownCandidate(dropdown, rawValue);
            const selectedValue = sourceValue || normalizeEditableDropdownValue(dropdown, rawValue);
            const optionValues = dropdown.options.slice();
            // 数量类计算结果可能不是初期默认值。只要是合法计算值，就动态加入候选并优先选中。
            if (dropdown.allowCalculatedValue && selectedValue && !optionValues.includes(selectedValue)) {
              optionValues.push(selectedValue);
            }
            optionValues.forEach((value) => {
              const option = document.createElement('option');
              option.value = value;
              option.textContent = value;
              control.appendChild(option);
            });
            control.value = selectedValue;
            // 原始值缺失时保持 originalValue 为空；仅显式配置的初期默认值会作为一次变更参与计算。
            control.dataset.originalValue = sourceValue;
            control.title = '请选择参数值';
          } else {
            control = document.createElement('input');
            control.type = 'text';
            control.value = rawValue;
            control.title = '可直接修改参数值';
          }
          control.className = 'param-table-input';
          control.dataset.resultEditable = rowIsEditable ? 'true' : 'false';
          if (!rowIsEditable) {
            control.disabled = true;
            control.title = '该参数不可修改（自动计算/初期默认）';
            valueTd.classList.add('readonly-param-value-cell');
          } else {
            control.title = control.tagName === 'SELECT' ? '可修改：请选择参数值' : '可修改：可直接修改参数值';
            valueTd.classList.add('editable-param-value-cell');
          }
          if (!Object.prototype.hasOwnProperty.call(control.dataset, 'originalValue')) {
            control.dataset.originalValue = control.value;
          }
          bindEditableParameterControl(table, tr, control);
          valueTd.appendChild(control);
        });
        if (meta.kind === 'equipment_result') ensureEquipmentParameterDraftToolbar(table);
        syncEditableParameterTableFromDom(table);
      });
    }

    function isCalculationIntent(prompt) {
      const compact = String(prompt || '').replace(/\s+/g, '');
      return /开始计算|公式计算|进行计算|确认计算/.test(compact);
    }

    function isParameterModifyIntent(prompt) {
      const compact = String(prompt || '').replace(/\s+/g, '');
      return /参数修改|修改参数|参数更新|更新参数|调整参数/.test(compact);
    }

    function shouldAttachPendingParameterEdits(prompt) {
      // 仅「开始计算」和显式「参数修改」才附带表格变更；
      // 模型设计（设计斜梯）、候选确认（回复 1/2/3）等指令不能被历史修改污染。
      return isCalculationIntent(prompt) || isParameterModifyIntent(prompt);
    }

    function equipmentLabelToParamAssignment(label, value) {
      const normalized = normalizeParamKey(label);
      if (!normalized || !value) return '';
      const mapping = {
        '锅炉正常排渣量': 'boiler_normal_slag_rate（锅炉正常排渣量，单位 t/h）',
        '锅炉最大排渣量': 'max_slag_rate（锅炉最大排渣量，单位 t/h）',
        '机组容量': 'boiler_capacity（机组容量，单位 MW）',
        '锅炉梳型密封板长度': 'comb_plate_length（锅炉梳型密封板长度，单位 mm）',
        '锅炉梳型密封板宽度': 'comb_plate_width（锅炉梳型密封板宽度，单位 mm）',
        '锅炉下膨胀量': 'boiler_down_expansion（锅炉下膨胀量，单位 mm）',
        '下联箱标高': 't1（下联箱标高，单位 mm）',
        '渣仓直径': 'd1（干渣仓直径 D1，单位 mm）',
        '干渣仓直径': 'd1（干渣仓直径 D1，单位 mm）',
        '渣仓顶标高': 'transfer_floor_height（转运层高度，单位 mm）',
        '渣井口数': 'slag_well_count（渣井口数，单位 个）',
        '渣井总容积': 'jv（渣井总容积，单位 m3）',
        '渣井容积': 'jv（渣井总容积，单位 m3）',
        '渣井最大储渣时间': 'slag_well_max_storage_time（渣井最大储渣时间，单位 h）',
        '干渣堆积密度': 'dry_slag_bulk_density（干渣堆积密度，单位 t/m3）',
        '干渣仓有效容积': 'cv1（干渣仓有效容积，单位 m3）',
        '干渣仓仓体锥段角度': 'cone_included_angle（干渣仓仓体锥段角度，单位 °）',
        '干渣仓卸料形式': 'unloading_type（干渣仓卸料形式，单位 -）',
        '卸料出力': 'unloading_rate（卸料出力，单位 t/h）',
        '车间内外高度差': 'ground_height_diff（车间内外高度差，单位 mm）',
        '车间内外地面高度差': 'ground_height_diff（车间内外高度差，单位 mm）',
        '渣仓转运层平台高度': 'transfer_floor_height（转运层高度，单位 mm）',
        '碎渣机连接装置连接管形式': 'conveyor_silo_connection_type（碎渣机连接装置连接管形式，单位 -）',
        '干渣仓中心线至锅炉中心线间距': 'boiler_silo_center_distance（锅炉中心线至渣仓中心线间距，单位 mm）',
        '锅炉中心线至渣仓中心线间距': 'boiler_silo_center_distance（锅炉中心线至渣仓中心线间距，单位 mm）',
        '锅炉梳型密封板连接螺栓高度差': 'bolt_height_diff（锅炉梳型密封板连接螺栓高度差，单位 mm）',
        '关断门平台类型': 'gate_platform_type（关断门平台类型，单位 -）',
        '渣仓转运层平台类型': 'silo_platform_type（渣仓转运层平台类型，单位 -）',
        '干渣仓扶梯组件类型': 'silo_ladder_component_type（干渣仓扶梯组件类型，单位 -）'
      };
      const key = Object.keys(mapping)
        .sort((left, right) => normalizeParamKey(right).length - normalizeParamKey(left).length)
        .find((name) => normalized.includes(normalizeParamKey(name)));
      if (!key) return '';
      return mapping[key] + '=' + value;
    }

    function buildEquipmentResultOverrideLines(label, value) {
      const name = normalizeEditableCellValue(label);
      const current = normalizeEditableCellValue(value);
      if (!name || !current) return [];
      const rawLine = name + '=' + current;
      const mapped = equipmentLabelToParamAssignment(name, current);
      if (mapped && mapped !== rawLine) return [rawLine, mapped];
      // 与聊天输入「干渣井型号 GZJ3401」一致，供工作流 _parse_calculation_result_updates 识别。
      return [rawLine];
    }

    function buildChangedParameterAssignmentLines() {
      if (!chat) return { lines: [], skippedEquipmentRows: [], equipmentOverrides: [], parameterOverrides: [] };
      const lines = [];
      const skippedEquipmentRows = [];
      const equipmentOverrides = [];
      const parameterOverrides = [];
      chat.querySelectorAll('table[data-editable-param-table="true"]').forEach((table) => {
        const headers = getMarkdownTableHeaders(table);
        const meta = detectEditableResultTable(headers);
        if (!meta) return;
        const draftState = meta.kind === 'equipment_result'
          ? equipmentParameterDraftState(table, false)
          : null;
        // 设备结果只能通过表格上的“确认修改并计算”提交，普通发送按钮不得夹带未确认草稿。
        if (draftState && (draftState.editing || draftState.submitting)) return;
        const paramIdIndex = findHeaderIndex(headers, ['参数标识', '参数id', '参数ID']);
        const paramNameIndex = findHeaderIndex(headers, ['参数中文名称', '参数名称', '参数名', '设备名称']);
        const labelIndex = meta.nameIndex >= 0 ? meta.nameIndex : findExactHeaderIndex(headers, ['参数', '设备名称']);
        const unitIndex = findHeaderIndex(headers, ['单位']);
        editableTableRows(table).forEach((tr) => {
          const cells = tr.querySelectorAll('td');
          const control = tr.querySelector('input.param-table-input, select.param-table-input');
          if (!control) return;
          const original = normalizeEditableCellValue(control.dataset.originalValue || '');
          const current = normalizeEditableCellValue(control.value);
          if (current === original) return;
          if (meta.kind === 'equipment_result') {
            const label = labelIndex >= 0
              ? normalizeEditableCellValue((cells[labelIndex] && cells[labelIndex].textContent) || '')
              : '';
            if (!label || !current) return;
            const overrideLines = buildEquipmentResultOverrideLines(label, current);
            if (overrideLines.length) {
              lines.push(...overrideLines);
              equipmentOverrides.push({ label, value: current });
            } else {
              skippedEquipmentRows.push(label);
            }
            return;
          }
          const key = paramIdIndex >= 0
            ? normalizeEditableCellValue((cells[paramIdIndex] && cells[paramIdIndex].textContent) || '')
            : '';
          const name = paramNameIndex >= 0
            ? normalizeEditableCellValue((cells[paramNameIndex] && cells[paramNameIndex].textContent) || '')
            : key;
          const unit = unitIndex >= 0
            ? normalizeEditableCellValue((cells[unitIndex] && cells[unitIndex].textContent) || '')
            : '-';
          if (!key || !current) return;
          lines.push(key + '（' + name + '，单位 ' + unit + '）=' + current);
          parameterOverrides.push({ key, name, value: current });
        });
      });
      return { lines, skippedEquipmentRows, equipmentOverrides, parameterOverrides };
    }

    function appendEditedParametersToPrompt(prompt) {
      syncAllEditableParameterTablesToSelection();
      const base = String(prompt || '').trim();
      const changes = buildChangedParameterAssignmentLines();
      const assignmentLines = changes.lines || [];
      const skipped = changes.skippedEquipmentRows || [];
      if (skipped.length) {
        setStatus('部分表格修改未能识别：' + skipped.slice(0, 4).join('、') + (skipped.length > 4 ? ' 等' : '') + '。');
      }
      if (!assignmentLines.length || !shouldAttachPendingParameterEdits(base)) {
        setPendingEquipmentResultOverrides([]);
        setPendingParameterResultOverrides([]);
        return base;
      }
      setPendingEquipmentResultOverrides(isCalculationIntent(base) ? (changes.equipmentOverrides || []) : []);
      setPendingParameterResultOverrides(isCalculationIntent(base) ? (changes.parameterOverrides || []) : []);
      if (isCalculationIntent(base)) {
        // 开始计算时只发送变更项的 key=值，避免整表 Markdown 被工作流误判为“参数修改回显”。
        return base + '\n\n' + assignmentLines.join('\n');
      }
      return base + '\n\n参数修改\n' + assignmentLines.join('\n');
    }

