#!/usr/bin/env python3
"""
Build world_map.bin for world_map_bin_loader.c.

This builder does NOT require high_accuracy_world_map.c.
It downloads/caches Natural Earth Admin-0 country polygons and writes the binary map.

Typical use:
    python3 build_world_map_bin.py world_map.bin

Higher/lower detail:
    python3 build_world_map_bin.py world_map.bin --scale 10m
    python3 build_world_map_bin.py world_map.bin --scale 50m
    python3 build_world_map_bin.py world_map.bin --scale 110m

Manual shapefile:
    python3 build_world_map_bin.py world_map.bin --countries-shp /path/to/ne_10m_admin_0_countries.shp
"""

import argparse
import pathlib
import struct
import urllib.request
import zipfile

import geopandas as gpd

MAGIC = b'WMBIN001'
VERSION = 2
LAYER_BORDER = 0
LAYER_COAST = 1
DEFAULT_CACHE_DIR = pathlib.Path('natural_earth_cache')

NE_URLS = {
    '10m': 'https://naturalearth.s3.amazonaws.com/10m_cultural/ne_10m_admin_0_countries.zip',
    '50m': 'https://naturalearth.s3.amazonaws.com/50m_cultural/ne_50m_admin_0_countries.zip',
    '110m': 'https://naturalearth.s3.amazonaws.com/110m_cultural/ne_110m_admin_0_countries.zip',
}

NAME_OVERRIDE = {
    'United States of America': 'United States',
    'Dem. Rep. Congo': 'Democratic Republic of the Congo',
    'Dominican Rep.': 'Dominican Republic',
    'W. Sahara': 'Western Sahara',
    'Falkland Is.': 'Falkland Islands',
    'Fr. S. Antarctic Lands': 'French Southern and Antarctic Lands',
    'Central African Rep.': 'Central African Republic',
    'Eq. Guinea': 'Equatorial Guinea',
    'eSwatini': 'Eswatini',
    'Palestine': 'Palestine',
    'Laos': 'Laos',
    'North Korea': 'North Korea',
    'South Korea': 'South Korea',
    'Czechia': 'Czech Republic',
    'Solomon Is.': 'Solomon Islands',
    'Bosnia and Herz.': 'Bosnia and Herzegovina',
    'Macedonia': 'North Macedonia',
    'S. Sudan': 'South Sudan',
    'N. Cyprus': 'Northern Cyprus',
    'Russia': 'Russia',
}

A3_TO_A2 = {
    'FJI':'fj','TZA':'tz','ESH':'eh','CAN':'ca','USA':'us','KAZ':'kz','UZB':'uz','PNG':'pg','IDN':'id',
    'ARG':'ar','CHL':'cl','COD':'cd','SOM':'so','KEN':'ke','SDN':'sd','TCD':'td','HTI':'ht','DOM':'do',
    'RUS':'ru','BHS':'bs','FLK':'fk','GRL':'gl','ATF':'tf','TLS':'tl','ZAF':'za','LSO':'ls','MEX':'mx',
    'URY':'uy','BRA':'br','BOL':'bo','PER':'pe','COL':'co','PAN':'pa','CRI':'cr','NIC':'ni','HND':'hn',
    'SLV':'sv','GTM':'gt','BLZ':'bz','VEN':'ve','GUY':'gy','SUR':'sr','ECU':'ec','PRI':'pr','JAM':'jm',
    'CUB':'cu','ZWE':'zw','BWA':'bw','NAM':'na','SEN':'sn','MLI':'ml','MRT':'mr','BEN':'bj','NER':'ne',
    'NGA':'ng','CMR':'cm','TGO':'tg','GHA':'gh','CIV':'ci','GIN':'gn','GNB':'gw','LBR':'lr','SLE':'sl',
    'BFA':'bf','CAF':'cf','COG':'cg','GAB':'ga','GNQ':'gq','ZMB':'zm','MWI':'mw','MOZ':'mz','SWZ':'sz',
    'AGO':'ao','BDI':'bi','ISR':'il','LBN':'lb','MDG':'mg','PSE':'ps','GMB':'gm','TUN':'tn','DZA':'dz',
    'JOR':'jo','ARE':'ae','QAT':'qa','KWT':'kw','IRQ':'iq','OMN':'om','VUT':'vu','KHM':'kh','THA':'th',
    'LAO':'la','MMR':'mm','VNM':'vn','PRK':'kp','KOR':'kr','MNG':'mn','IND':'in','BGD':'bd','BTN':'bt',
    'NPL':'np','PAK':'pk','AFG':'af','TJK':'tj','KGZ':'kg','TKM':'tm','IRN':'ir','SYR':'sy','ARM':'am',
    'SWE':'se','BLR':'by','UKR':'ua','POL':'pl','AUT':'at','HUN':'hu','MDA':'md','ROU':'ro','LTU':'lt',
    'LVA':'lv','EST':'ee','DEU':'de','BGR':'bg','GRC':'gr','TUR':'tr','ALB':'al','HRV':'hr','CHE':'ch',
    'LUX':'lu','BEL':'be','NLD':'nl','PRT':'pt','ESP':'es','IRL':'ie','NCL':'nc','SLB':'sb','NZL':'nz',
    'AUS':'au','LKA':'lk','CHN':'cn','TWN':'tw','ITA':'it','DNK':'dk','GBR':'gb','ISL':'is','AZE':'az',
    'GEO':'ge','PHL':'ph','MYS':'my','BRN':'bn','SVN':'si','FIN':'fi','SVK':'sk','CZE':'cz','ERI':'er',
    'JPN':'jp','PRY':'py','YEM':'ye','SAU':'sa','ATA':'aq','CYP':'cy','MAR':'ma','EGY':'eg','LBY':'ly',
    'ETH':'et','DJI':'dj','UGA':'ug','RWA':'rw','BIH':'ba','MKD':'mk','SRB':'rs','MNE':'me','TTO':'tt','SSD':'ss',
    'NOR':'no','FRA':'fr','XKX':'xk','KOS':'xk',
}

NAME_TO_A2 = {
    'Norway':'no', 'France':'fr', 'Kosovo':'xk', 'Somaliland':'so', 'N. Cyprus':'cy',
    'Russia':'ru', 'Russian Federation':'ru',
}


def row_get(row, *names):
    for name in names:
        if name in row.index:
            value = row[name]
            if value is None:
                continue
            value = str(value).strip()
            if value and value not in ('-99', 'nan', 'None'):
                return value
    return ''


def row_country_name(row):
    name = row_get(row, 'NAME', 'NAME_LONG', 'ADMIN', 'SOVEREIGNT', 'name', 'admin')
    return NAME_OVERRIDE.get(name, name)


def alpha2_for(row):
    a2 = row_get(row, 'ISO_A2', 'ADM0_A2', 'WB_A2', 'iso_a2')
    if a2 and a2 != '-99':
        return a2.lower()

    a3 = row_get(row, 'ISO_A3', 'ADM0_A3', 'WB_A3', 'GU_A3', 'SOV_A3', 'iso_a3')
    name = row_get(row, 'NAME', 'NAME_LONG', 'ADMIN', 'SOVEREIGNT', 'name', 'admin')
    if a3 in A3_TO_A2:
        return A3_TO_A2[a3]
    if name in NAME_TO_A2:
        return NAME_TO_A2[name]

    try:
        import pycountry
        obj = pycountry.countries.get(alpha_3=a3)
        if obj:
            return obj.alpha_2.lower()
    except Exception:
        pass

    return 'xx'


def ensure_countries_shp(scale, cache_dir, countries_shp=None):
    if countries_shp is not None:
        path = pathlib.Path(countries_shp)
        if not path.exists():
            raise SystemExit(f'countries shapefile does not exist: {path}')
        return path

    cache_dir = pathlib.Path(cache_dir)
    shp_name = f'ne_{scale}_admin_0_countries.shp'

    candidates = [
        pathlib.Path(shp_name),
        pathlib.Path('natural_earth_cache') / shp_name,
        pathlib.Path(__file__).resolve().parent / shp_name,
        pathlib.Path(__file__).resolve().parent / 'natural_earth_cache' / shp_name,
        cache_dir / shp_name,
    ]
    for path in candidates:
        if path.exists():
            return path

    if scale not in NE_URLS:
        raise SystemExit('scale must be 10m, 50m, or 110m')

    cache_dir.mkdir(parents=True, exist_ok=True)
    zip_path = cache_dir / f'ne_{scale}_admin_0_countries.zip'
    url = NE_URLS[scale]

    print('Natural Earth countries shapefile not found locally.')
    print('Downloading:', url)
    try:
        urllib.request.urlretrieve(url, zip_path)
    except Exception as e:
        raise SystemExit(
            'Failed to download Natural Earth country data.\n'
            f'Manually download and unzip this URL if needed:\n  {url}\n'
            f'Then rerun with:\n  --countries-shp /path/to/{shp_name}\n'
            f'Original error: {e}'
        )

    with zipfile.ZipFile(zip_path, 'r') as zf:
        zf.extractall(cache_dir)

    path = cache_dir / shp_name
    if path.exists():
        return path
    matches = list(cache_dir.rglob(shp_name))
    if matches:
        return matches[0]
    raise SystemExit(f'Downloaded zip, but {shp_name} was not found in {cache_dir}')


def iter_polygon_parts(geom):
    if geom is None or geom.is_empty:
        return
    if geom.geom_type == 'Polygon':
        yield geom
    elif geom.geom_type == 'MultiPolygon':
        for part in geom.geoms:
            if not part.is_empty:
                yield part
    elif geom.geom_type == 'GeometryCollection':
        for part in geom.geoms:
            yield from iter_polygon_parts(part)


def q100(value, lo, hi):
    value = float(value)
    if value < lo:
        value = lo
    if value > hi:
        value = hi
    return int(round(value * 100.0))


def append_polygon(poly, country_index, points, polygons):
    if poly is None or poly.is_empty:
        return False

    coords = list(poly.exterior.coords)
    if len(coords) < 4:
        return False
    if coords[0] != coords[-1]:
        coords.append(coords[0])

    start = len(points)
    minlon = 32767
    minlat = 32767
    maxlon = -32768
    maxlat = -32768
    last_q = None

    for idx, (lon, lat) in enumerate(coords):
        q = (q100(lon, -180.0, 180.0), q100(lat, -90.0, 90.0))
        is_last = idx == len(coords) - 1
        if last_q == q and not is_last:
            continue
        points.append(q)
        last_q = q
        minlon = min(minlon, q[0])
        minlat = min(minlat, q[1])
        maxlon = max(maxlon, q[0])
        maxlat = max(maxlat, q[1])

    if len(points) - start >= 2 and points[-1] != points[start]:
        points.append(points[start])

    count = len(points) - start
    if count < 4 or len(set(points[start:start + count])) < 3:
        del points[start:]
        return False

    polygons.append((start, count, country_index, minlon, minlat, maxlon, maxlat))
    return True


def add_string(strings, text):
    offset = len(strings)
    strings.extend(str(text).encode('utf-8') + b'\0')
    return offset


def write_bin(output_bin, points, polygons, countries, strings):
    # Detail lines are intentionally the same country exterior geometry.
    # This avoids internal lines inside countries and keeps hover/selection
    # matching exactly what is drawn.
    detail_points = list(points)
    detail_segments = []
    for start, count, country, minlon, minlat, maxlon, maxlat in polygons:
        detail_segments.append((start, count, LAYER_BORDER, minlon, minlat, maxlon, maxlat))

    with pathlib.Path(output_bin).open('wb') as f:
        f.write(MAGIC)
        f.write(struct.pack('<IIIIIII', VERSION,
                            len(points), len(polygons), len(countries), len(strings),
                            len(detail_points), len(detail_segments)))
        for lon, lat in points:
            f.write(struct.pack('<hh', lon, lat))
        for row in polygons:
            f.write(struct.pack('<IIHhhhh', *row))
        for row in countries:
            f.write(struct.pack('<IIIIhhhh', *row))
        f.write(bytes(strings))
        for lon, lat in detail_points:
            f.write(struct.pack('<hh', lon, lat))
        for row in detail_segments:
            f.write(struct.pack('<IIHhhhh', *row))


def build_from_shapefile(shp_path, output_bin):
    gdf = gpd.read_file(shp_path)
    if gdf.empty:
        raise SystemExit('countries shapefile loaded, but it is empty')

    points = []
    polygons = []
    countries = []
    strings = bytearray()
    names = []

    for _, row in gdf.iterrows():
        geom = row.geometry
        if geom is None or geom.is_empty:
            continue

        # Clean invalid geometries without adding artificial interior lines.
        if not geom.is_valid:
            try:
                geom = geom.buffer(0)
            except Exception:
                pass
        if geom is None or geom.is_empty:
            continue

        name = row_country_name(row)
        alpha2 = alpha2_for(row)

        poly_start = len(polygons)
        cminlon = 32767
        cminlat = 32767
        cmaxlon = -32768
        cmaxlat = -32768

        parts = list(iter_polygon_parts(geom))
        parts.sort(key=lambda p: p.area, reverse=True)
        country_index = len(countries)
        for part in parts:
            before = len(polygons)
            if append_polygon(part, country_index, points, polygons):
                _, _, _, minlon, minlat, maxlon, maxlat = polygons[-1]
                cminlon = min(cminlon, minlon)
                cminlat = min(cminlat, minlat)
                cmaxlon = max(cmaxlon, maxlon)
                cmaxlat = max(cmaxlat, maxlat)

        poly_count = len(polygons) - poly_start
        if poly_count <= 0:
            continue

        name_off = add_string(strings, name)
        alpha_off = add_string(strings, alpha2)
        countries.append((name_off, alpha_off, poly_start, poly_count,
                          cminlon, cminlat, cmaxlon, cmaxlat))
        names.append(name)

    if 'Russia' not in names and 'Russian Federation' not in names:
        print('WARNING: Russia was not found in the generated country table.')
    else:
        print('Russia: found')

    pathlib.Path(output_bin).parent.mkdir(parents=True, exist_ok=True)
    write_bin(output_bin, points, polygons, countries, strings)

    print('countries:', len(countries))
    print('country polygons:', len(polygons))
    print('points:', len(points))
    print('wrote:', output_bin)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('output_bin', nargs='?', type=pathlib.Path,
                        help='Output binary path, usually world_map.bin')
    parser.add_argument('legacy_output_bin', nargs='?', type=pathlib.Path,
                        help='Backward compatibility: if you pass high_accuracy_world_map.c world_map.bin, the first arg is ignored.')
    parser.add_argument('--scale', choices=['10m', '50m', '110m'], default='10m',
                        help='Natural Earth scale. 10m is highest detail; 50m is smaller/faster; 110m is lowest detail.')
    parser.add_argument('--countries-shp', type=pathlib.Path, default=None,
                        help='Optional path to an already downloaded ne_*_admin_0_countries.shp')
    parser.add_argument('--cache-dir', type=pathlib.Path, default=DEFAULT_CACHE_DIR,
                        help='Where Natural Earth downloads are cached')
    args = parser.parse_args()

    # Old command compatibility:
    #   python3 build_world_map_bin.py high_accuracy_world_map.c world_map.bin
    # The new builder does not use high_accuracy_world_map.c.
    if args.legacy_output_bin is not None:
        if str(args.output_bin).endswith('.c'):
            print('Note: high_accuracy_world_map.c is no longer needed; ignoring:', args.output_bin)
            output_bin = args.legacy_output_bin
        else:
            raise SystemExit('Use either: build_world_map_bin.py world_map.bin   or old-compatible: build_world_map_bin.py high_accuracy_world_map.c world_map.bin')
    elif args.output_bin is not None:
        output_bin = args.output_bin
    else:
        output_bin = pathlib.Path('world_map.bin')

    shp = ensure_countries_shp(args.scale, args.cache_dir, args.countries_shp)
    print('countries shapefile:', shp)
    build_from_shapefile(shp, output_bin)


if __name__ == '__main__':
    main()
