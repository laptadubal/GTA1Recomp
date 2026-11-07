#!/usr/bin/env python3
"""Audit CMP-authored road cells and legal traffic exits for every city.

This mirrors World::trafficRoadLevels and World::trafficNextRawHeight. The CMP
spec usually places block type and AI
direction flags above the visible lid; some retail road cells carry type 2 on
the lid block itself. Both encodings are included.

Run from the repository root:
    python3 tools/audit_road_network.py [NYC|SANB|MIAMI|ALL]
"""
import sys
from collections import Counter

sys.path.insert(0, 'tools')
from parse_formats import CMP, GRY  # noqa: E402
from audit_map_orientation import CITY_STYLE, road_lids  # noqa: E402


def top_cells(cmp_map):
    """Return (x,y)->(topZ,lid,road flags) in the engine's corrected frame."""
    cells = {}
    histogram = Counter()
    for y in range(256):
        for x in range(256):
            # parse_formats uses base[x*256+y]; swap args to match the engine.
            n = cmp_map.num_blocks(y, x)
            top = (0, 0, 0, 0)
            for z in range(n - 1, -1, -1):
                block = cmp_map.block_at(y, x, z)
                tm, ext, left, right, upper, lower, lid = block
                if tm == 0 and ext == 0 and not (left or right or upper or lower or lid):
                    continue
                if tm & 128 or lid == 0:
                    continue
                top = (z + 1, lid, tm, z)
                break
            cells[x, y] = top
            histogram[top[0]] += 1
    base_height = 1
    for height in range(1, 16):
        if histogram[height] > histogram[base_height]:
            base_height = height
    return cells, base_height


def audit(city):
    cmp_map = CMP('game_data/GTADATA/%s.CMP' % city)
    gry = GRY('game_data/GTADATA/%s.GRY' % CITY_STYLE[city])
    road_lid_ids = road_lids(gry)
    top, base_height = top_cells(cmp_map)
    roads = {}
    road_like_types = Counter()
    road_lids_not_grey_asphalt = 0
    minimap_only_asphalt = 0
    road_tags_with_other_type_above = []

    for (x, y), (height, lid, _tm, z) in top.items():
        if height <= 0:
            continue
        n = cmp_map.num_blocks(y, x)
        lid_block = cmp_map.block_at(y, x, z)
        above = cmp_map.block_at(y, x, z + 1) if z + 1 < n else None
        lid_type = ((lid_block[0] >> 4) & 7) if lid_block else -1
        above_type = ((above[0] >> 4) & 7) if above else -1
        # The files normally place the surface class above its visible lid,
        # but a small set of cells has type 2 on the lid and a different,
        # nonzero type above. Preserve the explicit surface road type and
        # retain this list as a diagnostic of stacked metadata combinations.
        if lid_type == 2 and above_type not in (-1, 0, 2):
            road_tags_with_other_type_above.append((x, y, lid, above_type))
        # Retail data uses both encodings. An explicit type-2 tag on the
        # visible surface wins over an unrelated stacked/decal block above.
        if lid_type == 2:
            flags, block_type = lid_block, lid_type
        else:
            flags, block_type = above, above_type
        # buildMinimap uses the visible block's own class when there is no
        # nonzero metadata block above it. Keep that resolution separate from
        # the traffic lookup below (which only needs to recognize type 2).
        minimap_type = (lid_type if lid_type == 2 else
                        above_type if above_type not in (-1, 0) else lid_type)
        road_like_types[minimap_type] += 1
        if block_type != 2:
            # buildMinimap falls back to the grayscale lid classifier only
            # when CMP has no surface class. These pixels look like road on
            # the map, while traffic correctly requires an authored type-2 tag.
            if minimap_type <= 0 and lid in road_lid_ids:
                minimap_only_asphalt += 1
            continue
        if lid not in road_lid_ids:
            # Diagnostic only: gameplay/minimap now trust CMP blockType=2,
            # while this color heuristic is still used for visual cleanup.
            road_lids_not_grey_asphalt += 1
        surface = base_height if height <= base_height + 1 else height
        direction_mask = (flags[0] & 15) if flags else 0
        if direction_mask == 0:
            current = cmp_map.block_at(y, x, z)
            direction_mask = current[0] & 15
        roads[x, y] = (surface, direction_mask, height,
                       (lid_block[0] >> 8) & 63)

    # The gameplay traffic graph must preserve every tagged road lid in a
    # column. Overpasses often stack a second road above a street; using only
    # the highest lid loses the lower route at crossings.
    road_levels = {}
    for y in range(256):
        for x in range(256):
            levels = []
            count = cmp_map.num_blocks(y, x)
            for z in range(count - 1, -1, -1):
                block = cmp_map.block_at(y, x, z)
                if not block:
                    continue
                tm, ext, left, right, upper, lower, lid = block
                if tm == 0 and ext == 0 and not (left or right or upper or lower or lid):
                    continue
                if tm & 128 or lid == 0:
                    continue
                above = cmp_map.block_at(y, x, z + 1) if z + 1 < count else None
                lid_type = (tm >> 4) & 7
                above_type = ((above[0] >> 4) & 7) if above else 0
                road_type = (lid_type if lid_type == 2 else
                             above_type if above_type else lid_type)
                if road_type != 2:
                    continue
                flags = block if lid_type == 2 else (above if above_type else block)
                mask = (flags[0] & 15) if flags else 0
                if mask == 0:
                    mask = tm & 15
                raw_height = z + 1
                shown_height = (base_height if raw_height <= base_height + 1
                                else raw_height)
                levels.append((shown_height, mask, raw_height,
                               (tm >> 8) & 63))
            if levels:
                road_levels[x, y] = levels

    # bit indices match DMA's up/down/left/right definition.
    directions = ((0, -1, 1), (0, 1, 2), (-1, 0, 4), (1, 0, 8))
    source_outgoing = 0
    no_geometric_neighbor = 0
    blocked_by_source_flags = 0
    linked = 0
    ramp_links = 0

    def ramp_along_x(value):
        return 5 <= value <= 8 or 25 <= value <= 40 or value in (43, 44)

    def ramp_along_y(value):
        return 1 <= value <= 4 or 9 <= value <= 24 or value in (41, 42)

    for (x, y), (surface, mask, raw_height, slope) in roads.items():
        geometric = 0
        source_options = 0
        for dx, dy, bit in directions:
            target = roads.get((x + dx, y + dy))
            if target is None:
                continue
            height_delta = abs(target[0] - surface)
            raw_delta = abs(target[2] - raw_height)
            ramp_join = (raw_delta == 1 and
                         ((dx != 0 and (ramp_along_x(slope) or
                                       ramp_along_x(target[3]))) or
                          (dy != 0 and (ramp_along_y(slope) or
                                       ramp_along_y(target[3])))))
            if height_delta > 1 and not ramp_join:
                continue
            geometric += 1
            source_allows = mask == 0 or bool(mask & bit)
            if source_allows:
                source_options += 1
                # DMA direction bits describe the allowed direction of travel
                # on this source cell; the destination's mask controls its
                # next move, not entry into the current cell.
                linked += 1
                if height_delta > 1 and ramp_join:
                    ramp_links += 1
        if source_options:
            source_outgoing += 1
        elif geometric == 0:
            no_geometric_neighbor += 1
        else:
            blocked_by_source_flags += 1

    level_outgoing = 0
    level_dead_ends = 0
    level_flag_blocked = 0
    level_links = 0
    level_ramp_links = 0
    level_node_count = sum(len(levels) for levels in road_levels.values())
    multi_level_columns = sum(len(levels) > 1 for levels in road_levels.values())
    level_adjacency = {
        (x, y, raw_height): set()
        for (x, y), levels in road_levels.items()
        for _surface, _mask, raw_height, _slope in levels
    }
    dead_end_samples = []
    blocked_samples = []
    for (x, y), source_levels in road_levels.items():
        for surface, mask, raw_height, slope in source_levels:
            geometric = 0
            outgoing = False
            for dx, dy, bit in directions:
                best_target = None
                best_raw_delta = 1000
                for target in road_levels.get((x + dx, y + dy), ()):
                    height_delta = abs(target[0] - surface)
                    raw_delta = abs(target[2] - raw_height)
                    ramp_join = (raw_delta == 1 and
                                 ((dx != 0 and (ramp_along_x(slope) or
                                               ramp_along_x(target[3]))) or
                                  (dy != 0 and (ramp_along_y(slope) or
                                               ramp_along_y(target[3])))))
                    if height_delta > 1 and not ramp_join:
                        continue
                    geometric += 1
                    if mask != 0 and not (mask & bit):
                        continue
                    if raw_delta < best_raw_delta:
                        best_raw_delta = raw_delta
                        best_target = target
                if best_target is not None:
                    outgoing = True
                    level_links += 1
                    source = (x, y, raw_height)
                    destination = (x + dx, y + dy, best_target[2])
                    level_adjacency[source].add(destination)
                    level_adjacency[destination].add(source)
                    height_delta = abs(best_target[0] - surface)
                    raw_delta = abs(best_target[2] - raw_height)
                    ramp_join = (raw_delta == 1 and
                                 ((dx != 0 and (ramp_along_x(slope) or
                                               ramp_along_x(best_target[3]))) or
                                  (dy != 0 and (ramp_along_y(slope) or
                                               ramp_along_y(best_target[3])))))
                    if height_delta > 1 and ramp_join:
                        level_ramp_links += 1
            if outgoing:
                level_outgoing += 1
            elif geometric:
                level_flag_blocked += 1
                if len(blocked_samples) < 12:
                    blocked_samples.append((x, y, raw_height, mask, slope))
            else:
                level_dead_ends += 1
                if len(dead_end_samples) < 12:
                    dead_end_samples.append((x, y, raw_height, mask, slope))

    # Weak connectivity ignores travel direction but preserves raw deck
    # identity. It distinguishes a directed one-way street from a road island
    # that cannot be reached through any legal CMP exit.
    unseen = set(level_adjacency)
    components = []
    while unseen:
        stack = [unseen.pop()]
        component = set(stack)
        while stack:
            node = stack.pop()
            for neighbour in level_adjacency[node]:
                if neighbour in unseen:
                    unseen.remove(neighbour)
                    component.add(neighbour)
                    stack.append(neighbour)
        components.append(component)
    components.sort(key=len, reverse=True)
    small_components = [component for component in components if len(component) <= 10]
    small_component_samples = []
    for component in small_components[:8]:
        xs = [node[0] for node in component]
        ys = [node[1] for node in component]
        small_component_samples.append(
            (len(component), min(xs), min(ys), max(xs), max(ys)))
    isolated_samples = [
        tuple(sorted(component)) for component in components if len(component) <= 3
    ]
    # Police route vertices (type 255) are authored waypoint loops. Count
    # which vertices land on the same CMP-tagged road surface used by runtime.
    route_pos = cmp_map.routeOff
    route_end = route_pos + cmp_map.routeSize
    police_routes = []
    roadblock_route_ids = set()
    while route_pos < route_end:
        vertex_count, route_type = cmp_map.d[route_pos], cmp_map.d[route_pos + 1]
        route_pos += 2
        vertices = [tuple(cmp_map.d[route_pos + i * 3:route_pos + i * 3 + 3])
                    for i in range(vertex_count)]
        route_pos += vertex_count * 3
        if route_type == 255:
            vertices = [v for v in vertices if v != (0, 0, 0)]
            if vertices:
                police_routes.append(vertices)
        elif route_type <= 253:
            roadblock_route_ids.add(route_type)
    route_vertices = sum(len(route) for route in police_routes)
    route_vertices_on_roads = sum(
        (x, y) in roads for route in police_routes for x, y, _z in route)
    route_levels_matched = 0
    route_levels_adjacent = 0
    route_levels_fallback = 0
    route_levels_unmatched = 0
    for route in police_routes:
        for x, y, route_z in route:
            levels = road_levels.get((x, y), ())
            preferred_raw_height = 5 - route_z
            if any(level[2] == preferred_raw_height for level in levels):
                route_levels_matched += 1
            else:
                adjacent_match = any(
                    any(level[2] == preferred_raw_height
                        for level in road_levels.get((x + dx, y + dy), ()))
                    for dx, dy, _bit in directions
                )
                if adjacent_match:
                    route_levels_adjacent += 1
                elif levels:
                    route_levels_fallback += 1
                else:
                    route_levels_unmatched += 1
    closed_police_routes = sum(route[0][:2] == route[-1][:2]
                               for route in police_routes)
    traffic_boxes = []
    for object_pos in range(cmp_map.objOff, cmp_map.objOff + cmp_map.objSize, 14):
        object_type = cmp_map.d[object_pos + 6]
        remap = cmp_map.d[object_pos + 7]
        route_id = int.from_bytes(cmp_map.d[object_pos + 8:object_pos + 10],
                                  'little')
        if object_type == 16 and remap < 128:
            traffic_boxes.append(route_id)
    linked_traffic_boxes = sum(route_id in roadblock_route_ids
                               for route_id in traffic_boxes)

    total_edges = len(roads) * 4
    print('%s: base height %d' % (city, base_height))
    print('  authored road cells (type 2): %d' % len(roads))
    print('  road cells by resolved CMP surface type: %s' %
          ', '.join('%s=%d' % (k, road_like_types[k])
                    for k in sorted(road_like_types)))
    print('  road cells with lids outside grey-asphalt heuristic: %d' %
          road_lids_not_grey_asphalt)
    print('  minimap asphalt fallback without CMP road tag: %d' %
          minimap_only_asphalt)
    print('  surface type-2 tags with another type above: %d' %
          len(road_tags_with_other_type_above))
    if road_tags_with_other_type_above:
        print('    samples (x,y,lid,aboveType): %s' %
              ', '.join(map(str, road_tags_with_other_type_above[:8])))
    print('  top-surface road cells with an allowed exit: %d/%d' %
          (source_outgoing, len(roads)))
    print('  no neighbor=%d source-flag blocked=%d' %
          (no_geometric_neighbor, blocked_by_source_flags))
    print('  directed neighbor links after source-cell flags: %d (of at most %d)' %
          (linked, total_edges))
    print('  ramp links restored by raw height+slope orientation: %d' % ramp_links)
    print('  traffic road levels: %d across %d multi-level columns' %
          (level_node_count, multi_level_columns))
    print('  road levels with an allowed exit: %d/%d; directed links=%d' %
          (level_outgoing, level_node_count, level_links))
    print('  road-level dead ends=%d; source-flag blocked=%d; ramp links=%d' %
          (level_dead_ends, level_flag_blocked, level_ramp_links))
    print('  weakly connected road components=%d; largest sizes=%s; '
          'components <=10=%d' %
          (len(components), ','.join(str(len(c)) for c in components[:5]),
           len(small_components)))
    if small_component_samples:
        print('    small samples (size,minX,minY,maxX,maxY): %s' %
              ', '.join(map(str, small_component_samples)))
    if isolated_samples:
        print('    small road islands (x,y,rawZ): %s' %
              '; '.join(map(str, isolated_samples[:8])))
    if dead_end_samples:
        print('    dead-end samples (x,y,rawZ,mask,slope): %s' %
              ', '.join(map(str, dead_end_samples)))
    if blocked_samples:
        print('    flag-blocked samples (x,y,rawZ,mask,slope): %s' %
              ', '.join(map(str, blocked_samples)))
    print('  police route loops: %d closed=%d; vertices on roads=%d/%d' %
          (len(police_routes), closed_police_routes, route_vertices_on_roads,
           route_vertices))
    print('  police route Z road levels: exact=%d adjacent-exact=%d '
          'fallback=%d no-road=%d' %
          (route_levels_matched, route_levels_adjacent,
           route_levels_fallback, route_levels_unmatched))
    print('  traffic-light boxes (type 16) linked to roadblock routes: %d/%d' %
          (linked_traffic_boxes, len(traffic_boxes)))


def main():
    cities = ('NYC', 'SANB', 'MIAMI')
    requested = sys.argv[1].upper() if len(sys.argv) > 1 else 'ALL'
    if requested == 'ALL':
        selected = cities
    elif requested in cities:
        selected = (requested,)
    else:
        raise SystemExit('usage: audit_road_network.py [NYC|SANB|MIAMI|ALL]')
    for city in selected:
        audit(city)


if __name__ == '__main__':
    main()
