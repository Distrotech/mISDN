<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method="text" version="1.0" encoding="UTF-8" indent="yes"/>

<!--
	Module: xhfc
	Options: debug=<number>
-->

<xsl:template name="XHFCmodule">

<xsl:call-template name="if-set">
 <xsl:with-param name="prefix"> debug=</xsl:with-param>
 <xsl:with-param name="val" select="@debug" />
 <xsl:with-param name="val-default">0</xsl:with-param>
</xsl:call-template>
<xsl:text>
</xsl:text>
</xsl:template>


<!--
	Card: xhfc
-->
<xsl:template name="XHFCcard">
<xsl:text>
</xsl:text>
</xsl:template>



<!--
	Port: xhfc
	Options: mode=(te|nt) line=(s0|up) link=(ptmp|ptp)
                 lineloop_b1=(yes|no) lineloop_b2=(yes|no) lineloop_d=(yes|no)
                 polx=(yes|no)
-->
<xsl:template name="XHFCport">

<xsl:text> layermask:</xsl:text>
<xsl:choose>
<xsl:when test="@mode='nt'">
 <xsl:text>3</xsl:text>
</xsl:when>
<xsl:when test="@capi='yes'">
 <xsl:text>0</xsl:text>
</xsl:when>
<xsl:otherwise>
 <xsl:text>15</xsl:text>
</xsl:otherwise>
</xsl:choose>

<xsl:text> protocol:</xsl:text>
<xsl:call-template name="if-match">
 <xsl:with-param name="val" select="@mode" />
 <xsl:with-param name="match-true">te</xsl:with-param>
 <xsl:with-param name="match-false">nt</xsl:with-param>
 <xsl:with-param name="val-true">2</xsl:with-param>
 <xsl:with-param name="val-false">18</xsl:with-param>
 <xsl:with-param name="val-default">34</xsl:with-param>
</xsl:call-template>
<xsl:text>+</xsl:text>
<xsl:call-template name="if-match">
 <xsl:with-param name="val" select="@link" />
 <xsl:with-param name="match-true">ptp</xsl:with-param>
 <xsl:with-param name="match-false">ptmp</xsl:with-param>
 <xsl:with-param name="val-true">1024</xsl:with-param>
 <xsl:with-param name="val-false">0</xsl:with-param>
 <xsl:with-param name="val-default">0</xsl:with-param>
</xsl:call-template>
<xsl:text>+</xsl:text>
<xsl:call-template name="if-match">
 <xsl:with-param name="val" select="@line" />
 <xsl:with-param name="match-true">up</xsl:with-param>
 <xsl:with-param name="match-false">s0</xsl:with-param>
 <xsl:with-param name="val-true">32</xsl:with-param>
 <xsl:with-param name="val-false">0</xsl:with-param>
 <xsl:with-param name="val-default">0</xsl:with-param>
</xsl:call-template>
<xsl:text>+</xsl:text>
<xsl:call-template name="if-match">
 <xsl:with-param name="val" select="@lineloop_b1" />
 <xsl:with-param name="match-true">yes</xsl:with-param>
 <xsl:with-param name="match-false">no</xsl:with-param>
 <xsl:with-param name="val-true">128</xsl:with-param>
 <xsl:with-param name="val-false">0</xsl:with-param>
 <xsl:with-param name="val-default">0</xsl:with-param>
</xsl:call-template>
<xsl:text>+</xsl:text>
<xsl:call-template name="if-match">
 <xsl:with-param name="val" select="@lineloop_b2" />
 <xsl:with-param name="match-true">yes</xsl:with-param>
 <xsl:with-param name="match-false">no</xsl:with-param>
 <xsl:with-param name="val-true">256</xsl:with-param>
 <xsl:with-param name="val-false">0</xsl:with-param>
 <xsl:with-param name="val-default">0</xsl:with-param>
</xsl:call-template>
<xsl:text>+</xsl:text>
<xsl:call-template name="if-match">
 <xsl:with-param name="val" select="@lineloop_d" />
 <xsl:with-param name="match-true">yes</xsl:with-param>
 <xsl:with-param name="match-false">no</xsl:with-param>
 <xsl:with-param name="val-true">512</xsl:with-param>
 <xsl:with-param name="val-false">0</xsl:with-param>
 <xsl:with-param name="val-default">0</xsl:with-param>
</xsl:call-template>
<xsl:text>+</xsl:text>
<xsl:call-template name="if-match">
 <xsl:with-param name="val" select="@polx" />
 <xsl:with-param name="match-true">yes</xsl:with-param>
 <xsl:with-param name="match-false">no</xsl:with-param>
 <xsl:with-param name="val-true">64</xsl:with-param>
 <xsl:with-param name="val-false">0</xsl:with-param>
 <xsl:with-param name="val-default">0</xsl:with-param>
</xsl:call-template>

<xsl:text> capi:</xsl:text>
<xsl:call-template name="if-match">
 <xsl:with-param name="val" select="@capi" />
 <xsl:with-param name="val-true">yes</xsl:with-param>
 <xsl:with-param name="val-false">no</xsl:with-param>
 <xsl:with-param name="val-default">no</xsl:with-param>
</xsl:call-template>



<xsl:text>
</xsl:text>
</xsl:template>

</xsl:stylesheet>
