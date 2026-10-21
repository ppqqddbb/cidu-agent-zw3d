from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

ROWS = [
    ["干渣井型号", "GZJ340", "写入模型参数属性，并带入图纸明细表"],
    ["渣仓型号", "GZC8x200", "写入模型参数属性，并带入图纸明细表"],
    ["渣仓直径", "8000mm", "用于后续模型搭建"],
    ["渣仓顶标高", "17506.89mm", "用于后续模型搭建和逻辑计算"],
    ["渣仓锥段高度", "6144mm", "用于后续模型搭建"],
    ["渣仓柱段高度", "4262.89mm", "用于后续模型搭建"],
    ["渣仓卸料类型", "两口", "用于后续模型搭建"],
    ["液压破碎关断门型号", "YPM326", "写入模型参数属性，并带入图纸明细表"],
    ["关断门口间距", "7000mm", "用于后续模型搭建"],
    ["关断门定位距离", "7000mm", "用于后续模型搭建"],
    ["渣井下口标高", "2815mm", "用于后续模型搭建"],
    ["渣井壁板上口标高", "5800mm", "用于后续模型搭建"],
    ["渣井壁板上口长度", "20405mm", "用于后续模型搭建"],
    ["渣井左右壁板角度", "54.7°", "用于后续模型搭建"],
    ["关断门箱体长度", "4220mm", "用于后续模型搭建"],
    ["关断门箱体高度", "2780mm", "用于后续模型搭建"],
    ["关断门箱体宽度", "740mm", "用于后续模型搭建"],
    ["膨胀节类型", "波纹型膨胀节", "用于后续模型搭建"],
    ["膨胀节长度", "4220mm", "用于后续模型搭建"],
    ["膨胀节宽度", "2504mm", "用于后续模型搭建"],
    ["膨胀节接口长度", "3030mm", "用于后续模型搭建"],
    ["膨胀节接口宽度", "1210mm", "用于后续模型搭建"],
    ["干渣机规格", "GLD16", "用于后续模型搭建和逻辑计算"],
    ["鳞斗干渣机型号", "GLD16x59", "写入模型参数属性，并带入图纸明细表"],
]


def build_rows_block() -> str:
  parts = ['def _fixed_equipment_selection_rows():\\n    return [\\n']
  for row in ROWS:
    parts.append(f'        [\\"{row[0]}\\", \\"{row[1]}\\", \\"{row[2]}\\"],\\n')
  parts.append('    ]\\n\\n\\n')
  return ''.join(parts)


def patch_yaml_file(path: Path) -> None:
  text = path.read_text(encoding='utf-8')
  start = text.find('def _fixed_equipment_selection_rows():')
  end = text.find('def _build_output(ctx, selected_keys):', start)
  if start < 0 or end < 0:
    raise RuntimeError(f'{path}: could not locate equipment rows block')
  new_text = text[:start] + build_rows_block() + text[end:]
  new_text = new_text.replace(
    '_markdown_table([\\"序号\\", \\"设备名称\\", \\"设备型号/参数\\", \\"取得途径\\"], _fixed_equipment_selection_rows())',
    '_markdown_table([\\"参数\\", \\"参数值\\", \\"后续应用\\"], _fixed_equipment_selection_rows())',
  )
  new_text = new_text.replace(
    '# 设备选型结果必须按标准 28 项模板逐项输出',
    '# 设备选型结果按标准参数项模板逐项输出，含后续应用列供模型参数一对一匹配',
  )
  new_text = new_text.replace(
    '_cidu_autofill_get(row, [\\"设备名称\\", \\"参数中文名称\\", \\"参数名称\\", \\"参数标识\\", \\"名称\\", \\"字段\\", \\"参数名\\"])',
    '_cidu_autofill_get(row, [\\"参数\\", \\"设备名称\\", \\"参数中文名称\\", \\"参数名称\\", \\"参数标识\\", \\"名称\\", \\"字段\\", \\"参数名\\"])',
  )
  new_text = new_text.replace(
    '_cidu_autofill_get(row, [\\"设备型号/参数\\", \\"数值\\", \\"提取数值\\", \\"参数值\\", \\"使用值\\", \\"识别结果\\", \\"型号\\", \\"结果\\"])',
    '_cidu_autofill_get(row, [\\"参数值\\", \\"设备型号/参数\\", \\"数值\\", \\"提取数值\\", \\"使用值\\", \\"识别结果\\", \\"型号\\", \\"结果\\"])',
  )
  path.write_text(new_text, encoding='utf-8')
  print(f'updated {path}')


def main() -> None:
  for path in ROOT.joinpath('docs').glob('*.yml'):
    if path.name.endswith('.yml'):
      try:
        patch_yaml_file(path)
      except RuntimeError as exc:
        if 'could not locate equipment rows block' in str(exc):
          continue
        raise


if __name__ == '__main__':
  main()
