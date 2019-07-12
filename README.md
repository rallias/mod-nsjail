mod_nsjail
==========

About
-----
mod_nsjail is an Apache module, based on mod_ruid2, to allow for an Apache process to run entirely within a namespaced environment, taking advantage of Linux namespaces to provide a greater (theoretical) level of security, similar in practice to how containerization products, such as LXC or Docker, do so.

Install
-------
 1. download and install latest libcap from here
 2. run `/apachedir/bin/apxs -a -i -l cap -c mod_nsjail.c`
 3. configure httpd.conf
 4. restart apache

Configuration
-------------

 `RMode config|stat` (default is config)
 `RUidGid user|#uid group|#gid` - when RMode is config, set to this uid and gid

 `RMinUidGid user|#uid group|#gid` - when uid/gid is < than min uid/gid set to default uid/gid
 `RDefaultUidGid user|#uid group|#gid`

 `RGroups group1 group2` - additional groups set via setgroups
 `@none` - clear all previous defined groups.

 `RDocumentChrRoot` - Set chroot directory and the document root inside

Example
-------
```
 LoadModule ruid2_module   modules/mod_ruid2.so
 User                     apache
 Group                    apache
 RMode                    stat
 RGroups                  apachetmp
 RDocumentChRoot          /home /example.com/public_html

 NameVirtualHost 192.168.0.1
 <VirtualHost example.com>
   ServerAdmin    webmaster@example.com
   RDocumentChRoot /home /example.com/public_html
   ServerName     example.com
   ServerAlias    www.example.com
   RMode          config		# unnecessary since config is the default
   RUidGid        user1 group1
   RGroups        apachetmp

   <Directory /home/example.com/public_html/dir>
       RMode stat
   </Directory>

   <Directory /home/example.com/public_html/dir/test>
       RMode config
       RUidGid user2 group2
       RGroups groups1
   </Directory>

   <Directory /home/example.com/public_html/dir/test/123>
       RUidGid user3 group3
   </Directory>

   <Location /yustadir>
       RMode config
       RUidGid user4 user4
       RGroups groups4
   </Location>

 </VirtualHost>

 <VirtualHost example.net>
   ServerAdmin    webmaster@example.net
   DocumentRoot   /home/example.net/public_html
   ServerName     example.net
   ServerAlias    www.example.net
 </VirtualHost>
```