    // 计算结果草稿确认后的依赖重算跟踪器。
    // 公式依赖关系由 Dify 公式节点实际执行，本地通过重算前后的原子快照差异判断：
    // 用户直接编辑的是“源参数”，随响应一起变化的是“关联参数”，其余为无关参数。
    const ciduAutomaticRecalculation = (() => {
      function strip(value) {
        return String(value == null ? '' : value).replace(/[`*_]/g, '').trim();
      }

      function canonicalKey(value) {
        return strip(value)
          .replace(/\[[^\]]*\]/g, '')
          .replace(/【[^】]*】/g, '')
          .replace(/[（(][^）)]*[）)]/g, '')
          .replace(/(?:毫米|mm|厘米|cm|米|m|度|°|个|口|节|台|套|组|种)$/i, '')
          .replace(/[:：=，,。；;\s_\-]/g, '')
          .toLowerCase();
      }

      function normalizedValue(value) {
        return strip(value).replace(/\s+/g, '').replace(/立方米|m³|m\^3/gi, 'm3');
      }

      function snapshotFromEntries(entries, revision = 0) {
        const values = {};
        (Array.isArray(entries) ? entries : []).forEach((entry) => {
          if (!entry) return;
          const name = strip(entry.original_name || entry.name || '');
          const key = canonicalKey(name);
          const value = strip(entry.value);
          if (!key || !value) return;
          values[key] = { key, name, value, normalized_value: normalizedValue(value) };
        });
        return { revision: Number(revision || 0), values };
      }

      function snapshotFromBridge(bridge) {
        if (!bridge || typeof bridge.debugState !== 'function') {
          return snapshotFromEntries([], 0);
        }
        const state = bridge.debugState() || {};
        const equipment = state.equipment || {};
        return snapshotFromEntries(equipment.entries || [], state.calculation_revision || equipment.revision || 0);
      }

      function normalizeChanges(changes) {
        const byKey = {};
        (Array.isArray(changes) ? changes : []).forEach((item) => {
          if (!item) return;
          const label = strip(item.label || item.name || item.key || '');
          const key = canonicalKey(label);
          if (!key) return;
          byKey[key] = {
            key,
            label,
            value: strip(item.value),
          };
        });
        return Object.values(byKey);
      }

      function compare(before, after, changes) {
        const previous = before && before.values ? before.values : {};
        const current = after && after.values ? after.values : {};
        const edited = normalizeChanges(changes);
        const editedKeys = new Set(edited.map((item) => item.key));
        const related = [];
        const unchanged = [];

        Object.keys(current).forEach((key) => {
          const currentItem = current[key];
          const previousItem = previous[key];
          if (!previousItem) return;
          if (previousItem.normalized_value !== currentItem.normalized_value) {
            if (!editedKeys.has(key)) {
              related.push({
                key,
                label: currentItem.name || previousItem.name || key,
                before: previousItem.value,
                after: currentItem.value,
              });
            }
          } else if (!editedKeys.has(key)) {
            unchanged.push({
              key,
              label: currentItem.name || previousItem.name || key,
              value: currentItem.value,
            });
          }
        });

        related.sort((left, right) => left.label.localeCompare(right.label, 'zh-CN'));
        return {
          edited,
          related,
          unchanged,
          before_revision: Number(before && before.revision || 0),
          after_revision: Number(after && after.revision || 0),
        };
      }

      function summary(diff) {
        const result = diff || { edited: [], related: [], unchanged: [] };
        const editedText = (result.edited || []).map((item) => (
          item.label + (item.value ? '=' + item.value : '')
        )).join('、') || '计算结果参数';
        const related = result.related || [];
        if (!related.length) {
          return '确认计算完成：' + editedText + '；未检测到依赖参数变化，其余参数保持原值。';
        }
        const labels = related.slice(0, 6).map((item) => item.label).join('、');
        const suffix = related.length > 6 ? ' 等' + related.length + '项' : '（' + related.length + '项）';
        return '确认计算完成：' + editedText + '；联动调整：' + labels + suffix + '，无关参数保持原值。';
      }

      return {
        canonicalKey,
        normalizedValue,
        snapshotFromEntries,
        snapshotFromBridge,
        normalizeChanges,
        compare,
        summary,
      };
    })();
