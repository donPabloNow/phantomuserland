package .ru.dz.test;

// it is impossible to use untyped var in numeric expr, but possible as object.

class base
{
    var s : String;
}


class child extnds base
{
	var s1;
	
	void method()
	{
	s = "Hello";
	s1 = s;
	}
}