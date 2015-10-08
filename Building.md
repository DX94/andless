# Prerequisites #

  * Android SDK and NDK v.1.6r1 or later
  * JRE & JDK 1.6
  * Eclipse 3.5
  * ADT plug-in for Eclipse, v0.9.5 or later
  * GNU make

# Details #

The following assumes that your operating system is Linux.
I didn't try this in Windows, but I think it should be straightforward.

  1. Install and setup the above tools if you didn't do it yet.
  1. Download the sources from this SVN:
```
svn checkout http://andless.googlecode.com/svn/trunk andLess
```
  1. Go to your NDK root directory
  1. mkdir apps/lossless
  1. Create the file "apps/lossless/Application.mk" containing two lines:
```
APP_PROJECT_PATH := $(call my-dir)/project
APP_MODULES      := alac ape flac wav wv mpc lossless
```
  1. In apps/lossless, make a link called "project" pointing to the directory where you've unpacked the andLess sources (full path)
  1. For NDK version 1.6, change the following line in build-binary.mk <br>
<pre><code>diff -r android-ndk-1.6_r1.orig/build/core/build-binary.mk android-ndk-1.6_r1/build/core/build-binary.mk<br>
166c166<br>
&lt; $(LOCAL_BUILT_MODULE): PRIVATE_STATIC_LIBRARIES := $(static_libraries)<br>
---<br>
&gt; $(LOCAL_BUILT_MODULE): PRIVATE_WHOLE_STATIC_LIBRARIES := $(static_libraries)<br>
</code></pre>
<ol><li>Go to the NDK root dir and "make APP=lossless" to get "liblossless.so" in its proper place, so that it'll get added to the .apk when you build the java code.<br>
</li><li>Import the Java part of this project to Eclipse and build it.<br>
You should get an installable apk package containing the library.</li></ol>

Update for Windows users: andLess was reported to compile with ndk-<a href='https://code.google.com/p/andless/source/detail?r=4'>r4</a> & 5 using <a href='http://code.google.com/p/mini-cygwin'>http://code.google.com/p/mini-cygwin</a> (thanks <b>vrix yan</b>)