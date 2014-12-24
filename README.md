ddjvu
=====
ddjvu is a wrapper around djvulibre that provides you easy asynchronous access 
to the document content.
To work with ddjvu you have to implement delegate class for ddjvu::IBmp<T> and ddjvu::IBmpFactory<T>.
ddjvu/File.h is the only file you shold use in your programm to work with document.
