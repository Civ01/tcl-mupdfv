#!/usr/bin/env python3
"""Writes the test PDFs in tests/samples/. The PDFs are committed; rerun this
only to change them. Needs Pillow (for the JPEG 2000 image)."""
import io
import os
import zlib

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "samples")


def write_pdf(name, objects):
    """objects: list of bytes (object bodies); object 1 must be the catalog."""
    out = bytearray(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
    offsets = []
    for i, body in enumerate(objects, 1):
        offsets.append(len(out))
        out += b"%d 0 obj\n" % i + body + b"\nendobj\n"
    xref = len(out)
    out += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objects) + 1)
    for off in offsets:
        out += b"%010d 00000 n \n" % off
    out += b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objects) + 1, xref)
    with open(os.path.join(OUT, name), "wb") as f:
        f.write(out)


def stream(data, extra=b""):
    data = zlib.compress(data)
    return b"<< /Length %d /Filter /FlateDecode %s >>\nstream\n" % (len(data), extra) + data + b"\nendstream"


def font(base):
    return b"<< /Type /Font /Subtype /Type1 /BaseFont /%s >>" % base


def text_pdf():
    # Two pages. Page 1 uses all standard fonts, page 2 has a second paragraph.
    p1 = (b"BT /F1 20 Tf 40 760 Td (Hello Helvetica) Tj ET\n"
          b"BT /F2 20 Tf 40 730 Td (Times Roman line) Tj ET\n"
          b"BT /F3 20 Tf 40 700 Td (Courier mono) Tj ET\n"
          b"BT /F4 20 Tf 40 670 Td (abg) Tj ET\n"
          b"BT /F5 20 Tf 40 640 Td (3456) Tj ET\n"
          b"BT /F6 20 Tf 40 610 Td (Bold Helvetica) Tj ET\n"
          b"0 0 1 rg 40 500 200 60 re f\n")
    p2 = b"BT /F1 14 Tf 40 760 Td (Second page text) Tj ET\n"
    res = b"<< /Font << /F1 5 0 R /F2 6 0 R /F3 7 0 R /F4 8 0 R /F5 9 0 R /F6 10 0 R >> >>"
    write_pdf("text.pdf", [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R 11 0 R] /Count 2 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents 4 0 R /Resources " + res + b" >>",
        stream(p1),
        font(b"Helvetica"), font(b"Times-Roman"), font(b"Courier"), font(b"Symbol"),
        font(b"ZapfDingbats"), font(b"Helvetica-Bold"),
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents 12 0 R /Resources " + res + b" >>",
        stream(p2),
    ])


def jpx_pdf():
    from PIL import Image
    img = Image.new("RGB", (16, 16), (200, 30, 30))
    buf = io.BytesIO()
    img.save(buf, "JPEG2000")
    jpx = buf.getvalue()
    content = b"q 200 0 0 200 100 500 cm /Im1 Do Q\nBT /F1 14 Tf 40 760 Td (Page with a JPEG 2000 image) Tj ET\n"
    write_pdf("jpx.pdf", [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents 4 0 R "
        b"/Resources << /XObject << /Im1 5 0 R >> /Font << /F1 6 0 R >> >> >>",
        stream(content),
        b"<< /Type /XObject /Subtype /Image /Width 16 /Height 16 /Filter /JPXDecode /Length %d >>\nstream\n" % len(jpx)
        + jpx + b"\nendstream",
        font(b"Helvetica"),
    ])


def annot_pdf():
    # A square annotation with its own appearance (drawn green), and one without (not drawn).
    ap = b"0 0.6 0 rg 0 0 100 100 re f"
    write_pdf("annot.pdf", [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents 4 0 R /Annots [5 0 R 7 0 R] "
        b"/Resources << /Font << /F1 8 0 R >> >> >>",
        stream(b"BT /F1 14 Tf 40 760 Td (Annotations) Tj ET\n"),
        b"<< /Type /Annot /Subtype /Square /Rect [100 500 200 600] /C [1 0 0] /AP << /N 6 0 R >> >>",
        stream(ap, b"/Type /XObject /Subtype /Form /BBox [0 0 100 100]"),
        b"<< /Type /Annot /Subtype /Square /Rect [300 500 400 600] /C [0 0 1] /IC [0 0 1] >>",
        font(b"Helvetica"),
    ])


def cjk_pdf():
    # Two non-embedded Japanese fonts: one with the built-in Identity-H encoding,
    # one with UniJIS-UCS2-H, which this build does not include.
    def type0(enc, desc):
        return (b"<< /Type /Font /Subtype /Type0 /BaseFont /MS-Mincho /Encoding /%s /DescendantFonts [%s] >>" % (enc, desc))
    cid = (b"<< /Type /Font /Subtype /CIDFontType2 /BaseFont /MS-Mincho "
           b"/CIDSystemInfo << /Registry (Adobe) /Ordering (Japan1) /Supplement 6 >> "
           b"/FontDescriptor 8 0 R /DW 1000 >>")
    desc = (b"<< /Type /FontDescriptor /FontName /MS-Mincho /Flags 6 /FontBBox [0 -141 1000 859] "
            b"/ItalicAngle 0 /Ascent 859 /Descent -141 /CapHeight 700 /StemV 80 >>")
    content = (b"BT /F1 24 Tf 40 760 Td <0843 0844> Tj ET\n"         # CIDs in Adobe-Japan1
               b"BT /F2 24 Tf 40 700 Td <3042 3044> Tj ET\n"          # UCS-2 code points
               b"BT /F3 14 Tf 40 640 Td (CJK test) Tj ET\n")
    write_pdf("cjk.pdf", [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents 4 0 R "
        b"/Resources << /Font << /F1 5 0 R /F2 6 0 R /F3 9 0 R >> >> >>",
        stream(content),
        type0(b"Identity-H", b"7 0 R"),
        type0(b"UniJIS-UCS2-H", b"7 0 R"),
        cid,
        desc,
        font(b"Helvetica"),
    ])


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    text_pdf()
    jpx_pdf()
    annot_pdf()
    cjk_pdf()
    print("wrote", sorted(os.listdir(OUT)))
