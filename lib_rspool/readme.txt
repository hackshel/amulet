rspool2

rspool2�������ص㣺
1�������ǰ�汾���ṹ���Ӻ��������Ķ�����
2����ǰ�汾��rspool��ҪԤ���ƶ��ش�С��Ŀǰ�汾����Ҫ
3��ʹ����ĸ������grid�����ͬ������Դ����һ��
4���ṩ��group_foreach��all_foreach���ܰ���ѭ������ȫ��ѭ����

ʹ��ʾ����


RSPOOL *rsp;

rsp = rspool_new();
/*         ��� ��� ��Դ��� */
rspool_put(rsp, 0, myhande); 
new_handle = rspool_get(rsp, 0);

while ((new_handle = rspool_group_foreach(rsp, 3)) != NULL) {
    printf("===>foreach: %s\n", new_handle);
}

rspool_del(rsp);

������ο� test_main.c
