#!/usr/bin/env python3
"""Export completed IKOS DBs using IKOS's own summary/report aggregation."""
from pathlib import Path
import json
import sys
from ikos.output_db import OutputDatabase
from ikos import report
from ikos.enums import Result, CheckKind, CheckerName


def normalize_info(value, db):
    if isinstance(value, list):
        return [normalize_info(item, db) for item in value]
    if not isinstance(value, dict):
        return value
    result = {}
    for key, item in value.items():
        if key == 'fun_id':
            result['function'] = db.functions[item].name
        elif key == 'points_to':
            locations = []
            for point in item:
                point = dict(point)
                if 'id' in point:
                    location = db.memory_locations[point.pop('id')]
                    info = location.load_info()
                    if location.kind == 2 and info:
                        identity = {'kind': 'function', 'name': db.functions[info['id']].name}
                    elif location.kind == 7 and info:
                        call = db.statements[info['call_id']]
                        identity = {'kind': 'allocation', 'file': call.file_path(),
                                    'function': call.function().name,
                                    'line': call.line, 'column': call.column}
                    else:
                        identity = {'kind': location.kind, 'info': info}
                    point['location'] = identity
                locations.append(normalize_info(point, db))
            result[key] = sorted(locations, key=lambda item: json.dumps(item, sort_keys=True))
        else:
            result[key] = normalize_info(item, db)
    return result


def main():
    for raw in sys.argv[1:]:
        path = Path(raw)
        db = OutputDatabase(str(path))
        summary = report.generate_summary(db)
        settings = db.load_settings()
        diagnostics = []
        full = report.generate_report(db, status_filter=None)
        for item in full.statement_reports:
            statement = item.statement()
            operands = item.load_operands()
            diagnostics.append(dict(
                file=statement.file_path(), function=statement.function().name,
                function_display=statement.function().pretty_name(),
                line=statement.line, column=statement.column,
                statement_id=statement.id,
                kind=CheckKind.short_name(item.kind), status=Result.str(item.status),
                operands=[{'number': p.num, 'repr': p.operand.repr} for p in operands] if operands else None,
                info=normalize_info(item.load_info(), db), contexts=len(item.call_context_ids)))
        by_checker = {}
        for checker, status, count in db.con.execute(
                'select checker,status,count(*) from checks group by checker,status'):
            by_checker.setdefault(CheckerName.short_name(checker), {})[Result.str(status)] = count
        data = dict(export_schema=2, summary=vars(summary), settings=settings,
                    raw_checks_by_checker=by_checker, reports=diagnostics)
        path.with_suffix('.export.json').write_text(json.dumps(data, indent=2))
        db.close()
        print(path, vars(summary))


if __name__ == '__main__':
    main()
