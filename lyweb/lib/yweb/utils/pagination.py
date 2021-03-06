import re, urlparse, urllib
from mako.template import Template


PAGE_TEMPLATE = '''<div class="pagination">
  % if cur_page > 1:
  <a href="${ page_url(cur_page -1) }"><span class="endside">${ _("Prev") }<span></a>
  % else:
  <span class="endside">${ _("Prev") }</span>
  % endif

  % for p in plist:
  % if p == cur_page:
  <span class="page current">${ p }</span>
  % elif p == notexist_page:
  <span>...</span>
  % else:
  <a href="${ page_url(p) }"><span class="page">${ p }</span></a>
  % endif
  % endfor

  % if cur_page < page_sum:
  <a href="${ page_url(cur_page + 1) }"><span class="endside">${ _("Next") }</span></a>
  % else:
  <span class="endside">${ _("Next") }</span>
  % endif

  % if ps_range:
  <span class="psize-select">
    %for s in ps_range:
    <% current = 'current' if ps == s else '' %>
    <a class="${ current }" href="${ psize_url(s) }">${ s }</a>
    %endfor
  </span>
  % endif
</div>'''


def pagination( url, total, ps, cur, list_size=5,
                ps_range=[12, 24, 48] ):

    if total <= ps:
        return ''

    ps_range = [x for x in ps_range if x < total]

    page_sum = total / ps
    if ( total % ps ): page_sum += 1

    notexist_p = page_sum + 1


    last_p = page_sum
    start = ( cur / (list_size + 1) ) * list_size + 1
    end = start + list_size
    if end > last_p: end = last_p

    plist = range(start, end + 1)

    if end < last_p:
        plist.extend( [notexist_p, last_p] )

    if cur > list_size:
        plist.insert(0, notexist_p)
        plist.insert(0, 1)

    def _page_url(cur):
        return page_url(url, cur)

    d = { 'total': total, 'plist': plist,  'ps': ps,
          'cur_page': cur, 'page_sum': page_sum,
          'notexist_page': notexist_p,
          'page_url': _page_url }

    if ps_range:
        def _psize_url(cur):
            return psize_url(url, cur)
        d['psize_url'] = _psize_url
        d['ps_range'] = ps_range

    t = Template( PAGE_TEMPLATE )
    return t.render(**d)


def page_url(uri, cur):

    if '?' not in uri:
        return uri + '?p=%s' % cur

    path, params = uri.split('?')
    new = []
    find_p = False
    for k, v in urlparse.parse_qsl( params ):
        if k == 'p':
            v = cur
            find_p = True
        new.append( (k, v) )

    if not find_p:
        new.append( ('p', cur) )

    return '?'.join([path, urllib.urlencode(new)])


def psize_url(uri, cur):

    if '?' not in uri:
        return uri + '?ps=%s' % cur

    path, params = uri.split('?')
    new = []
    find_ps = False
    for k, v in urlparse.parse_qsl( params ):
        if k == 'ps':
            v = cur
            find_ps = True
        if k == 'p':
            v = 1
        new.append( (k, v) )

    if not find_ps:
        new.append( ('ps', cur) )

    return '?'.join([path, urllib.urlencode(new)])

